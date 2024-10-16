
#include <stdio.h>
#ifdef LITE_CURL

#include "lite_util.h"




typedef struct {
    lite_loop_t * ctx;
    union{
        uv_handle_t handle;
        uv_poll_t poll;
    };
    curl_socket_t sockfd;
} m_sock_t;




static void  curl_read_info(lite_loop_t * ctx){
    printf("MULTI READ INFO");
}

static void  timer_cb(uv_timer_t * handle){
    lite_loop_t *ctx = handle->data;
    int running_handles;
    curl_multi_socket_action(ctx->multi.handle, CURL_SOCKET_TIMEOUT, 0,&running_handles);
    curl_read_info(ctx);
}

static int mtimer_cb(CURLM *multi, long timeout_ms,lite_loop_t *ctx){
    (void)multi;
    if (timeout_ms < 0)
        uv_timer_stop(&ctx->multi.timer);
    else {
        if (timeout_ms == 0)
            timeout_ms = 1; /* 0 means call curl_multi_socket_action asap but NOT within the callback itself */
        uv_timer_start(&ctx->multi.timer, timer_cb, timeout_ms,
                   0); /* do not repeat */
    }
    return 0;
}

static void close_cb(uv_handle_t *handle){
  m_sock_t *msock =handle->data;
  free(msock);
}



static void  poll_cb(uv_poll_t *handle, int status, int events){
    int running_handles;
    int flags = 0;
    m_sock_t *msock =handle->data;
    (void)status;
    if(events & UV_READABLE)
        flags |= CURL_CSELECT_IN;
    if(events & UV_WRITABLE)
        flags |= CURL_CSELECT_OUT;
    curl_multi_socket_action(msock->ctx->multi.handle, msock->sockfd, flags,
                            &running_handles);
    curl_read_info(msock->ctx);
}


static int msocket_cb(CURL *easy, curl_socket_t s, int action,lite_loop_t *ctx,void *socketp){
    m_sock_t *msock;
    int events = 0;
    (void)easy;

    switch (action) {
    case CURL_POLL_IN:
    case CURL_POLL_OUT:
    case CURL_POLL_INOUT:
        if (socketp){
            msock=socketp;
        }else{
            msock=malloc(sizeof(m_sock_t));
            msock->sockfd=s;
            msock->ctx=ctx;
            uv_poll_init_socket(&ctx->loop,&msock->poll,s);
            msock->poll.data = msock;
        }
        curl_multi_assign(ctx->multi.handle, s,msock);
        if (action != CURL_POLL_IN)
            events |= UV_WRITABLE;
        if (action != CURL_POLL_OUT)
            events |= UV_READABLE;
        uv_poll_start(&msock->poll, events, poll_cb);
        break;
    case CURL_POLL_REMOVE:
        if (socketp) {
            uv_poll_stop(&((m_sock_t *)socketp)->poll);
            uv_close(&((m_sock_t *)socketp)->handle, close_cb);
            curl_multi_assign(ctx->multi.handle, s, NULL);
        }
        break;
    default: abort();
    }
    return 0;
}


void lite_multi_init(lite_loop_t * ctx){
    ctx->multi.handle=curl_multi_init();
    if (!ctx->multi.handle) lite_uv_throw(ctx->L, UV_ENOMEM);
    int rc=uv_timer_init(&ctx->loop, &ctx->multi.timer);
    if (rc<0) lite_uv_throw(ctx->L, rc);
    ctx->multi.timer.data=ctx;
    curl_multi_setopt(ctx->multi.handle, CURLMOPT_SOCKETFUNCTION, msocket_cb);
    curl_multi_setopt(ctx->multi.handle, CURLMOPT_SOCKETDATA, ctx);
    curl_multi_setopt(ctx->multi.handle, CURLMOPT_TIMERFUNCTION, mtimer_cb);
    curl_multi_setopt(ctx->multi.handle, CURLMOPT_TIMERDATA, ctx);
    //int running_handles;
    //rc=curl_multi_socket_action(ctx->multi.handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    //if (rc!=CURLM_OK) lite_multi_throw(ctx->L, rc);
}

void lite_multi_clean(lite_loop_t * ctx){
    // stop timer
    // close all easy
    // free multi
}


#endif