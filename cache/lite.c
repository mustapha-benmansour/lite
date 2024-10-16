
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <uv.h>



#include <llhttp.h>
#include <uv/version.h>


#include "deps/ws_parser/ws_parser.h"

#ifdef LITE_CURL
#include "M_easy.h"
#endif







#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <uv.h>





#ifdef LITE_CURL
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#endif











































































































































//------------------------  LITE_SIGNAL









//------------------------  LITE_TIMER





//------------------------  LITE_STREAM












/*static void lite_uv_read_http_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf){
    M_handle_t * h=stream->data;
    M_context_t * ctx = (M_context_t*)stream->loop->data;
    lua_State * L = ctx->L;
    if (nread>0 || nread==UV_EOF){
        int rc;
        if (h->http->is_ws){
            if (nread>0){
                rc=ws_parser_execute(&h->http->wp,&h->http->ws,&h->http,buf->base,nread);
                if (rc!=WS_OK){
                    if (RF_ISSET(h->http->error)){
                        RF_GET(h->http->error)
                        RF_UNSET(h->http->error) 
                        lite_context_push_lua_error(&ctx->errors, L);
                        uv_stop(&ctx->loop);
                    }else{
                        RF_GET(h->on_primary)
                        lua_pushnil(L);
                        lua_pushstring(L, ws_parser_error(rc));
                        lua_pushinteger(L, rc);
                        if (lua_pcall(L, 3,0,0)){
                            lite_context_push_lua_error(&ctx->errors, L);
                            uv_stop(&ctx->loop);
                        }
                    }
                    return ;
                }
                // not error
            }else{
                // client disconnected
            }
            return ;
        }
        if (nread>0){
            rc=llhttp_execute(&h->http->hp, buf->base,nread);
        }else
            rc=llhttp_finish(&h->http->hp);
        if (buf->base)
            free(buf->base);
        if (rc!=HPE_OK && rc!=HPE_PAUSED_H2_UPGRADE && rc!=HPE_PAUSED_UPGRADE && rc!=HPE_PAUSED){
            //HPE_PAUSED it is also an error (because we didnt provide pause function)
            if (RF_ISSET(h->http->error)){
                RF_GET(h->http->error)
                RF_UNSET(h->http->error) 
                lite_context_push_lua_error(&ctx->errors, L);
                uv_stop(&ctx->loop);
            }else{
                RF_GET(h->on_primary)
                lua_pushnil(L);
                lua_pushstring(L,llhttp_errno_name(rc));
                lua_pushinteger(L, rc);
                if (lua_pcall(L, 3,0,0)){
                    lite_context_push_lua_error(&ctx->errors, L);
                    uv_stop(&ctx->loop);
                }
            }            
        }
        return ;
    }
    if (buf->base)
        free(buf->base);
    if (nread==0) 
        return ;
    // nread<0 && nread!=UV_EOF
    lua_pushnil(L);
    lua_pushstring(L,uv_err_name(nread));
    lua_pushinteger(L, nread);
    if (lua_pcall(L, 3,0,0)){
        lite_context_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}*/









static int M_stream_use_ws_parser(lua_State * L){
    M_handle_t * h = *((M_handle_t **)lua_touserdata(L, 1));
    if (h->http){
        if (h->http->state==1)
            h->http->state=2;
        else if (h->http->state==0){
            free(h->http);
            h->http=NULL;
        }
    }
    if (h->ws) return 0;
    h->ws=malloc(sizeof(M_ws_t));
    if (!h->ws) return M_err_throw(L, UV_ENOMEM);
    //h->http->enabled_cbs=cbs;
    h->ws->parent=h;
    h->ws->error=LUA_NOREF;
    h->ws->state=0;
    //if (!h->http) return luaL_error(L, "required to call this from read_http_start/read_ws_start cb");
    //luaL_checktype(L, 2, LUA_TFUNCTION);
    //RF_UNSET_IFSET(h->on_primary)
    //lua_settop(L, 2);
    //RF_SET(h->on_primary)
    h->ws->wc.on_control_begin=M_ws_control_begin_cb;
    h->ws->wc.on_control_payload=M_ws_control_payload_cb;
    h->ws->wc.on_control_end=M_ws_control_end_cb;

    h->ws->wc.on_data_begin=M_ws_data_begin_cb;
    h->ws->wc.on_data_payload=M_ws_data_payload_cb;
    h->ws->wc.on_data_end=M_ws_data_end_cb;
    ws_parser_init(&h->ws->wp);
    return 0;
}
static int M_stream_use_http_parser(lua_State * L){
    M_handle_t * h = *((M_handle_t **)lua_touserdata(L, 1));
    if (h->ws){
        if (h->ws->state==1)
            h->ws->state=2;
        else if (h->ws->state==0){
            free(h->ws);
            h->ws=NULL;
        }
    }
    if (h->http) return 0;
    //uint8_t cbs=luaL_checkint(L, 3);
    h->http=malloc(sizeof(M_http_t));
    if (!h->http) return M_err_throw(L, UV_ENOMEM);
    //h->http->enabled_cbs=cbs;
    h->http->parent=h;
    h->http->error=LUA_NOREF;
    h->http->state=0;
    llhttp_settings_init(&h->http->hs);
    h->http->hs.on_message_begin=M_http_message_begin_cb;
    h->http->hs.on_url=M_http_url_cb;
    h->http->hs.on_status=M_http_status_cb;
    h->http->hs.on_method=M_http_method_cb;
    h->http->hs.on_version=M_http_version_cb;
    h->http->hs.on_header_field=M_http_header_field_cb;
    h->http->hs.on_header_value=M_http_header_value_cb;
    h->http->hs.on_chunk_extension_name=M_http_chunk_extension_name_cb;
    h->http->hs.on_chunk_extension_value=M_http_chunk_extension_value_cb;
    h->http->hs.on_headers_complete=M_http_headers_complete_cb;
    h->http->hs.on_body=M_http_body_cb;
    h->http->hs.on_message_complete=M_http_message_complete_cb;
    h->http->hs.on_url_complete=M_http_url_complete_cb;
    h->http->hs.on_status_complete=M_http_status_complete_cb;
    h->http->hs.on_method_complete=M_http_method_complete_cb;
    h->http->hs.on_version_complete=M_http_version_complete_cb;
    h->http->hs.on_header_field_complete=M_http_header_field_complete_cb;
    h->http->hs.on_header_value_complete=M_http_header_value_complete_cb;
    h->http->hs.on_chunk_extension_name_complete=M_http_chunk_extension_name_complete_cb;
    h->http->hs.on_chunk_extension_value_complete=M_http_chunk_extension_value_complete_cb;
    h->http->hs.on_chunk_header=M_http_chunk_header_cb;
    h->http->hs.on_reset=M_http_reset_cb;
    llhttp_init(&h->http->hp, HTTP_REQUEST,&h->http->hs);
    h->http->hp.data=h->http;
    return 0;
}



/*static int M_stream_write_http_head(lua_State * L){
    luaL_checktype(L, 2, LUA_TTABLE);

}*/















//------------------------  LITE_TCP







/*
static void  M_timer_cb(uv_timer_t * handle){
    M_handle_t * h=handle->data;
    M_context_t * ctx = (M_context_t*)handle->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    if (lua_pcall(L, 0,0,0)){
        lite_context_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int M_tcp_connect(lua_State * L){
    M_handle_t * h = *((M_handle_t **)lua_touserdata(L, 1));
    const char * ip=luaL_checkstring(L, 2);
    int port=luaL_checkint(L, 3);
    if (lua_isfunction(L, 4)){
        RF_UNSET_IFSET(h->on_primary)
        lua_settop(L, 4);
        RF_SET(h->on_primary)
    }
    if (RF_ISNOTSET(h->on_primary))
        return M_err_throw(L, UV_EINVAL);
    int rc=uv_timer_start(&h->timer, lite_uv_timer_cb,timeout,repeat);
    if (rc<0) return M_err_throw(L, rc);
    return 0;
}

static int M_tcp_stop(lua_State * L){
    M_handle_t * h = *((M_handle_t **)lua_touserdata(L, 1));
    uv_timer_stop(&h->timer);// never fail
    return 0;
}
*/




//---------------------------------LOOP





/*
static int M_multi_socket_cb(CURL *easy, curl_socket_t s, int action, void *userp, void *socketp) {

}

static void lite_multi_timer_cb(CURLM *multi, long timeout_ms, void *userp) {
    if (timeout_ms <= 0)
        timeout_ms = 1; 
    uv_timer_start(&timeout, on_timeout, timeout_ms, 0);
}
*/











