
#ifndef LITE_UTIL_H
#define LITE_UTIL_H

#include <lua.h>
#include <lauxlib.h>

#include <stdint.h>
#include <string.h>
#include <uv.h>

#include <stdlib.h>



#ifdef LITE_CJSON
int luaopen_cjson_safe(lua_State *L);
#endif

#ifdef LITE_CURL
#include <curl/curl.h>
#include <curl/multi.h>
#include <curl/easy.h>
#endif

#ifdef LITE_LLHTTP
#include "../deps/llhttp/include/llhttp.h"
#endif

#ifdef LITE_WSPARSER
#include "../deps/ws_parser/ws_parser.h"
#endif


#define LITE_MAX_PENDING_WREQS 10

#if LUA_VERSION_NUM == 501
#define LUA_OK 0
void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup);
#endif



//string.h
static inline int lite_os_throw(lua_State * L,int rc){
    lua_pushstring(L, strerror(rc));
    return lua_error(L);
}

//uv.h

static inline int lite_uv_throw(lua_State * L,int rc){
    lua_pushstring(L, uv_err_name(rc));
    return lua_error(L);
}


typedef struct lite_handle_t lite_handle_t;

typedef struct lite_http_t lite_http_t;

#ifdef LITE_WSPARSER
typedef struct  {
    uint8_t state;//0:not_running 1:runing 2:parser changed during callback
    ws_parser_t p;
    ws_parser_callbacks_t s;
    char buf[256];
    uint16_t of;
    uint16_t sz;
    uint8_t cbs;
    ws_frame_type_t tp;
    int error;
} lite_ws_t;
#endif

#ifdef LITE_LLHTTP
struct lite_http_t {
    uint8_t state;//0:not_running 1:runing 2:parser changed during callback
    llhttp_t p;
    llhttp_settings_t s;
    char buf[4096]; 
    uint16_t of;
    uint16_t sz;
    uint16_t m_of;
    uint8_t  m_sz;

    uint16_t u_of;
    uint16_t  u_sz;

    uint16_t v_of;
    uint8_t  v_sz;

    uint16_t s_of;
    uint8_t  s_sz;

    uint8_t h_sz;
    
    uint16_t hk_of[50];
    uint8_t hk_sz[50];

    uint16_t hv_of[50];
    uint16_t hv_sz[50];

    //uint16_t max_body_sz;
    //uint16_t max_file_sz;
    //uint8_t max_files;

    int error;
};
#endif


typedef struct lite_req_t lite_req_t;
struct lite_req_t{
    union{
        uv_req_t req;
        uv_write_t write;
        uv_shutdown_t shutdown;
    };
    uv_buf_t buf;
    int on_primary;
    lite_req_t * next;
    lite_req_t * prev;
};



struct lite_handle_t{
    union{
        uv_handle_t handle;
        uv_timer_t timer;
        uv_signal_t signal;
        uv_stream_t stream;
        uv_tcp_t tcp;
    };
#ifdef LITE_LLHTTP
    lite_http_t * http;
#endif
#ifdef LITE_WSPARSER
    lite_ws_t * ws;
#endif
    int on_close;
    int on_primary;
    lite_handle_t ** lua_obj_ud;
    lite_req_t * head;
    lite_req_t * tail;
    uint8_t reqs_size;
};


typedef enum {
    LITE_ASYNC_ERR_TNONE=0,
    LITE_ASYNC_ERR_TLUA=1,
    LITE_ASYNC_ERR_TOS=2,
    LITE_ASYNC_ERR_TUV=3,
#ifdef LITE_CURL
    LITE_ASYNC_ERR_TCURLMULTI=4,
    LITE_ASYNC_ERR_TCURLEASY=5,
#endif
}lite_async_err_type_t;

typedef struct {
    uint8_t len;
    int err[10];
    lite_async_err_type_t err_type[10];
}lite_async_errs_t;



typedef struct lite_loop_t lite_loop_t;
struct lite_loop_t{
    int unexpected_gc_len;
    lua_State * L;
    uv_loop_t loop;
    lite_async_errs_t errors;
#ifdef LITE_CURL
    struct {
        CURLM * handle;
        uv_timer_t timer;
    } multi;
#endif
} ;



#define LITE_VERSION_MAJOR 2
#define LITE_VERSION_MINOR 1
#define LITE_VERSION_PATCH 0


#define RF_ISSET(var) var!=LUA_NOREF
#define RF_ISNOTSET(var) var==LUA_NOREF
#define RF_UNSET(var) luaL_unref(L, LUA_REGISTRYINDEX,var);
#define RF_GET(var) lua_rawgeti(L, LUA_REGISTRYINDEX,var);
#define RF_SET(var) var=luaL_ref(L, LUA_REGISTRYINDEX);
#define RF_UNSET_IFSET(var) if (RF_ISSET(var)) RF_UNSET(var)



//lite.c
int lite_error_nomem(lua_State * L);
int lite_error_nomem_throw(lua_State * L);
inline int lite_success(lua_State * L){
    lua_pushboolean(L, 1);
    return 1;
}
inline int lite_error_invalid_arg(lua_State * L){
    lua_pushnil(L);
    lua_pushliteral(L, "EINVAL");
    return 2;
}



//lite_loop.c
int lite_loop_gc(lua_State * L);
int lite_loop_run(lua_State * L);
int lite_loop_stop(lua_State * L);
void lite_loop_push_lua_error(lite_async_errs_t * errors,lua_State * L);
void lite_loop_push_lua_error_ref(lite_async_errs_t * errors,lua_State * L,int ref);



//lite_handle.c
#define LITE_HANDLE_MT "lite.handle"
void lite_handle_link_req_append(lite_handle_t * h,lite_req_t * r);
void lite_handle_unlink_req(lite_handle_t * h,lite_req_t * r);
void  lite_handle_close_cb(uv_handle_t * handle);
lite_handle_t ** lite_handle(lua_State * L);
int lite_handle_gc(lua_State * L);
void lite_handle_reg(lua_State * L);



//lite_stream.c
#define LITE_STREAM_MT "lite.stream"
void lite_stream_reg(lua_State * L);

//lite_tcp.c
void lite_tcp_reg(lua_State * L);
int lite_tcp(lua_State * L);


#ifdef LITE_LLHTTP
//lite_http.c
void lite_http_free(lua_State * L,lite_http_t *http);
int lite_http_use_parser(lua_State * L,lite_handle_t * h);
int lite_http_status_name(lua_State * L);
#endif

#ifdef LITE_WSPARSER
//lite_ws.c
void lite_ws_free(lua_State * L,lite_ws_t *ws);
int lite_ws_use_parser(lua_State * L,lite_handle_t * h);
int lite_ws_build_frame(lua_State * L);
#endif



//lite_timer.c
void lite_timer_reg(lua_State * L);
int lite_timer(lua_State * L);


//lite_signal.c
void lite_signal_reg(lua_State * L);
int lite_signal(lua_State * L);






#ifdef LITE_CURL

//lite_multi.c
void lite_multi_push_error(lua_State * L,CURLMcode rc);
void lite_multi_init(lite_loop_t * ctx);
void lite_multi_clean(lite_loop_t * ctx);

//lite_easy.c
void lite_easy_reg(lua_State * L);
int lite_easy(lua_State * L);
void lite_easy_done_cb(lite_loop_t * ctx,CURL * easy,CURLcode rc);

inline int lite_loop_easy_throw(lua_State * L,int rc){
    lua_pushstring(L, curl_easy_strerror(rc));
    return lua_error(L);
}

#endif



#endif