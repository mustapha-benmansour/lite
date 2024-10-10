#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <uv.h>
#include <lua.h>
#include <lauxlib.h>

#include <llhttp.h>
#include <uv/version.h>


#include "deps/ws_parser/ws_parser.h"








#ifdef LITE_CURL
#include <curl/curl.h>
#include <curl/multi.h>
#endif



#define LITE_MAX_PENDING_WREQS 10
#if LUA_VERSION_NUM == 501
static void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup)
{
	luaL_checkstack(L, nup + 1, "too many upvalues");
	for (; l->name != NULL; l++) {
		int i;
		lua_pushstring(L, l->name);
		for (i = 0; i < nup; i++) {
			lua_pushvalue(L, -(nup + 1));
		}
		lua_pushcclosure(L, l->func, nup);
		lua_settable(L, -(nup + 3));
	}
	lua_pop(L, nup);
}
#endif

#define LITE_VERSION_MAJOR 1
#define LITE_VERSION_MINOR 0
#define LITE_VERSION_PATCH 0

#define LITE_TIMER_MT "lite.timer"
#define LITE_SIGNAL_MT "lite.signal"
#define LITE_TCP_MT "lite.tcp"


#define RF_ISSET(var) var!=LUA_NOREF
#define RF_ISNOTSET(var) var==LUA_NOREF
#define RF_UNSET(var) luaL_unref(L, LUA_REGISTRYINDEX,var);
#define RF_GET(var) lua_rawgeti(L, LUA_REGISTRYINDEX,var);
#define RF_SET(var) var=luaL_ref(L, LUA_REGISTRYINDEX);
#define RF_UNSET_IFSET(var) if (RF_ISSET(var)) RF_UNSET(var)




typedef struct lite_uv_handle_s lite_uv_handle_t;
typedef struct lite_http_s lite_http_t;
typedef struct lite_ws_s lite_ws_t;




struct lite_http_s {
    uint8_t state; // 0 none 1 running 2 request_close 
    llhttp_t hp;
    llhttp_settings_t hs;
    lite_uv_handle_t * parent;
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

struct lite_ws_s {
    uint8_t state; // 0 none 1 running 2 request_close 
    ws_parser_t wp;
    ws_parser_callbacks_t wc;
    lite_uv_handle_t * parent;
    char buf[256];
    uint16_t of;
    uint16_t sz;
    uint8_t cbs;
    int error;
};



typedef struct lite_uv_req_s lite_uv_req_t;
struct lite_uv_req_s{
    union{
        uv_req_t req;
        uv_write_t write;
        uv_shutdown_t shutdown;
    };
    uv_buf_t buf;
    int on_primary;
    lite_uv_req_t * next;
    lite_uv_req_t * prev;
};



struct lite_uv_handle_s{
    union{
        uv_handle_t handle;
        uv_timer_t timer;
        uv_signal_t signal;
        uv_stream_t stream;
        uv_tcp_t tcp;
    };
    int on_close;
    int on_primary;
    lite_uv_handle_t ** lua_obj_ud;

    lite_http_t * http;
    lite_ws_t * ws;
    

    lite_uv_req_t * head;
    lite_uv_req_t * tail;
    uint8_t reqs_size;
};






typedef enum {
    LITE_ERRNO_TNONE,
    LITE_ERRNO_TLUA,
    LITE_ERRNO_TOS,
    LITE_ERRNO_TUV,
}LITE_ERRNO_T;



typedef struct {
    int len;
    int error[10];
    LITE_ERRNO_T error_type[10];
}lite_errors_t;







typedef struct {
    int unexpected_gc_len;
    lua_State * L;
    uv_loop_t loop;
    lite_errors_t errors;
#ifdef LITE_CURL
    CURLM * curl_mhandle;
#endif
} lite_context_t;





static inline int lite_uv_err_throw(lua_State * L,int rc){
    lua_pushstring(L, uv_strerror(rc));
    return lua_error(L);
}

static inline void lite_context_push_lua_error(lite_errors_t * errors,lua_State * L){
    int len=errors->len;
    if (len<10){
        RF_SET(errors->error[len])
        errors->error_type[len]=LITE_ERRNO_TLUA;
        errors->len++;
    }else
        lua_pop(L, 1);
}

static inline void lite_context_push_lua_error_ref(lite_errors_t * errors,lua_State * L,int ref){
    int len=errors->len;
    if (len<10){
        errors->error[len]=ref;
        errors->error_type[len]=LITE_ERRNO_TLUA;
        errors->len++;
    }else
        RF_UNSET(ref)
}



static void  lite_uv_close_cb(uv_handle_t * handle);
static void lite_uv_walk_cb(uv_handle_t *handle, void *arg){
    (void)arg;
    if (!uv_is_closing(handle))
        uv_close(handle, lite_uv_close_cb);
}

static int lite_context_gc(lua_State * L){
    lite_context_t * ctx=lua_touserdata(L, 1);
    while (uv_loop_close(&ctx->loop)) {
        uv_walk(&ctx->loop, lite_uv_walk_cb, NULL);
        uv_run(&ctx->loop, UV_RUN_DEFAULT);// ignore err
    }
    return 0;
}

static int lite_version(lua_State * L){
    lua_pushfstring(L,
    "lite/v%d.%d.%d libuv/v%d.%d.%d llhttp/v%d.%d.%d",
    LITE_VERSION_MAJOR,LITE_VERSION_MINOR,LITE_VERSION_PATCH,
    UV_VERSION_MAJOR,UV_VERSION_MINOR,UV_VERSION_PATCH,
    LLHTTP_VERSION_MAJOR,LLHTTP_VERSION_MINOR,LLHTTP_VERSION_PATCH);
    return 1;
}

static int lite_http_status_name(lua_State * L){
    int status=luaL_checkint(L, 1);
    lua_pushstring(L, llhttp_status_name(status));
    return 1;
}


static void lite_uv_handle_link_req_append(lite_uv_handle_t * h,lite_uv_req_t * r){
    if (h->tail) {  
        h->tail->next = r;
        r->prev = h->tail;
        r->next = NULL;
        h->tail = r;
    } else { // empty list
        r->next=NULL;
        r->prev=NULL;
        h->head = h->tail = r;
    }
    h->reqs_size++;
}
static void lite_uv_handle_unlink_req(lite_uv_handle_t * h,lite_uv_req_t * r){
    if (r == h->head) {
        h->head = r->next;
        if (h->head) {
            h->head->prev = NULL;
        } else {  // empty list
            h->tail = NULL;
        }
    }else if (r == h->tail) { 
        h->tail = r->prev;
        if (h->tail) {
            h->tail->next = NULL;
        } else {  // empty list
            h->head = NULL;
        }
    } else {  // middle
        r->prev->next = r->next;
        r->next->prev = r->prev;
    }
    h->reqs_size--;
}







static void  lite_uv_close_cb(uv_handle_t * handle){
    lite_uv_handle_t * h=handle->data;
    lite_context_t * ctx = (lite_context_t*)handle->loop->data;
    lua_State * L = ctx->L;
    RF_UNSET(h->on_primary)
    if (RF_ISSET(h->on_close)){
        RF_GET(h->on_close)
        RF_UNSET(h->on_close)
        if (lua_pcall(L, 0,0,0))
            lite_context_push_lua_error(&ctx->errors,L);
    }
    if (h->http){
        RF_UNSET_IFSET(h->http->error)
        free(h->http);
    }
    if (h->ws){
        RF_UNSET_IFSET(h->ws->error)
        free(h->ws);
    }
    // a pending reqs related to this handle still there ?
    // not supposed to happen (libuv will call cb with ECANCEL ?????) note: freeing req is already implemented in cb
    if (h->head){
        // libuv bug ??? 
        // ok free reqs anyway (this not our problem)
        // may cause segmentation fault
        while (h->head) {
            lite_uv_req_t * r=h->head;
            lite_uv_handle_unlink_req(h, r);
            RF_UNSET(r->on_primary)
            free(r);
        }       
    }
    if (*h->lua_obj_ud)
        *h->lua_obj_ud=NULL;
    free(h);
}



static int lite_uv_handle_gc(lua_State * L){
    lite_uv_handle_t ** hp = lua_touserdata(L, 1);
    if (*hp){
        lite_context_t * ctx = (*hp)->handle.loop->data;
        ctx->unexpected_gc_len++;
        uv_stop(&ctx->loop);
    }
    return 0;
}


static int lite_uv_handle_on_close(lua_State * L){
    lite_uv_handle_t ** hp = lua_touserdata(L, 1);
    luaL_checktype(L, 2,LUA_TFUNCTION);
    RF_UNSET_IFSET((*hp)->on_close)
    lua_settop(L, 2);
    RF_SET((*hp)->on_close);
    return 0;
}

static int lite_uv_handle_close(lua_State * L){
    lite_uv_handle_t ** hp = lua_touserdata(L, 1);
    lua_settop(L, 1);
    lua_pushnil(L);
    lua_setmetatable(L, -2);
    if (!uv_is_closing(&(*hp)->handle))
        uv_close(&(*hp)->handle, lite_uv_close_cb);
    *hp=NULL;
    return 0;
}

static void lite_uv_handle_reg(lua_State * L){
    // abstract class
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg lite_handle_mt[] = {
        {"close",lite_uv_handle_close},
        {"on_close",lite_uv_handle_on_close},
        {NULL,NULL}
    };
    luaL_setfuncs(L, lite_handle_mt, 0);
    // keep handle_mt
}





//------------------------  LITE_SIGNAL

static int lite_signal(lua_State * L){
    lite_context_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    lite_uv_handle_t * h=malloc(sizeof(lite_uv_handle_t));
    if (!h) return lite_uv_err_throw(L, UV_ENOMEM);
    lite_uv_handle_t ** hp=lua_newuserdata(L, sizeof(lite_uv_handle_t));
    if (!h) {
        free(h);
        return lite_uv_err_throw(L, UV_ENOMEM);
    }
    *hp=h;
    h->lua_obj_ud=hp;
    h->on_primary=LUA_NOREF;
    h->on_close=LUA_NOREF;
    h->head=NULL;
    h->tail=NULL;
    h->reqs_size=0;
    h->http=NULL;
    h->ws=NULL;
    luaL_getmetatable(L, LITE_SIGNAL_MT);
    lua_setmetatable(L, -2);
    int rc=uv_signal_init(&ctx->loop, &h->signal);
    if (rc<0) return lite_uv_err_throw(L, rc);
    h->signal.data=h;
    return 1;
}

static void  lite_uv_signal_cb(uv_signal_t * handle,int signum){
    lite_uv_handle_t * h=handle->data;
    lite_context_t * ctx = (lite_context_t*)handle->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    lua_pushinteger(L, signum);
    if (lua_pcall(L, 1,0,0)){
        lite_context_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int lite_signal_start(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    int signum=luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    RF_UNSET_IFSET(h->on_primary)
    lua_settop(L, 3);
    RF_SET(h->on_primary)
    int rc=uv_signal_start(&h->signal,lite_uv_signal_cb,signum);
    if (rc<0) return lite_uv_err_throw(L, rc);
    return 0;
}

static int lite_signal_stop(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    uv_signal_stop(&h->signal);// never fail
    return 0;
}


static void lite_signal_reg(lua_State * L){
    // handle_mt at -1
    luaL_newmetatable(L, LITE_SIGNAL_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg lite_signal_mt[] = {
        {"__gc",lite_uv_handle_gc},
        {"start",lite_signal_start},
        {"stop",lite_signal_stop},
        {NULL,NULL}
    };
    luaL_setfuncs(L, lite_signal_mt, 0);
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2); // setmetatable(signal_mt,handle_mt)
    lua_pop(L, 1);
    // keep handle_mt
}

//------------------------  LITE_TIMER

static int lite_timer(lua_State * L){
    lite_context_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    lite_uv_handle_t * h=malloc(sizeof(lite_uv_handle_t));
    if (!h) return lite_uv_err_throw(L, UV_ENOMEM);
    lite_uv_handle_t ** hp=lua_newuserdata(L, sizeof(lite_uv_handle_t));
    if (!h) {
        free(h);
        return lite_uv_err_throw(L, UV_ENOMEM);
    }
    *hp=h;
    h->lua_obj_ud=hp;
    h->on_primary=LUA_NOREF;
    h->on_close=LUA_NOREF;
    h->head=NULL;
    h->tail=NULL;
    h->reqs_size=0;
    h->http=NULL;
    h->ws=NULL;
    luaL_getmetatable(L, LITE_TIMER_MT);
    lua_setmetatable(L, -2);
    int rc=uv_timer_init(&ctx->loop, &h->timer);
    if (rc<0) return lite_uv_err_throw(L, rc);
    h->timer.data=h;
    return 1;
}

static void  lite_uv_timer_cb(uv_timer_t * handle){
    lite_uv_handle_t * h=handle->data;
    lite_context_t * ctx = (lite_context_t*)handle->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    if (lua_pcall(L, 0,0,0)){
        lite_context_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int lite_timer_start(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    int timeout=luaL_checkinteger(L, 2);
    int repeat=luaL_checkinteger(L,3);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    RF_UNSET_IFSET(h->on_primary)
    lua_settop(L, 4);
    RF_SET(h->on_primary)
    int rc=uv_timer_start(&h->timer, lite_uv_timer_cb,timeout,repeat);
    if (rc<0) return lite_uv_err_throw(L, rc);
    return 0;
}

static int lite_timer_stop(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    uv_timer_stop(&h->timer);// never fail
    return 0;
}



static void lite_timer_reg(lua_State * L){
    // handle_mt at -1
    luaL_newmetatable(L, LITE_TIMER_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg lite_timer_mt[] = {
        {"__gc",lite_uv_handle_gc},
        {"start",lite_timer_start},
        {"stop",lite_timer_stop},
        {NULL,NULL}
    };
    luaL_setfuncs(L, lite_timer_mt, 0);
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2); // setmetatable(timer_mt,handle_mt)
    lua_pop(L, 1);
    // keep handle_mt
}


//------------------------  LITE_STREAM

static void lite_uv_connection_cb(uv_stream_t *stream,int status){
    lite_uv_handle_t * h=stream->data;
    lite_context_t * ctx = (lite_context_t*)stream->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    int n;
    if (status<0){
        lua_pushnil(L);
        lua_pushstring(L, uv_err_name(status));
        lua_pushinteger(L, status);
        n=3;
    }else{
        lua_pushboolean(L, 1);
        n=1;
    }
    if (lua_pcall(L, n,0,0)){
        lite_context_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int lite_uv_stream_listen(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    int backlog=luaL_checkint(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    RF_UNSET_IFSET(h->on_primary)
    lua_settop(L, 3);
    RF_SET(h->on_primary)
    int rc=uv_listen(&h->stream,backlog,lite_uv_connection_cb);
    if (rc<0) return lite_uv_err_throw(L, rc);
    return 0;
}

static int lite_uv_stream_accept(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    lite_uv_handle_t * h2 = *((lite_uv_handle_t **)lua_touserdata(L, 2));
    int rc=uv_accept(&h->stream,&h2->stream);
    if (rc<0) return lite_uv_err_throw(L, rc);
    return 1;
}



static inline int  lite_http_pcall_in_cb(lite_http_t * http,lua_State * L,int n){
    if (lua_pcall(L, n, 0, 0)){
        RF_SET(http->error)
        return HPE_USER;
    }
    return HPE_OK;
}



static int lite_http_message_begin_cb(llhttp_t * hp){
    lite_http_t * http = hp->data;
    lite_uv_handle_t * h= http->parent;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    http->of=0;
    http->sz=0;
    http->m_sz=0;
    http->u_sz=0;
    http->v_sz=0;
    http->s_sz=0;
    http->h_sz=0;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "message_begin",13);
    return lite_http_pcall_in_cb(http,L,1);
}

#define LH_CB_SCRIPT(KEY) \
    lite_http_t * http = hp->data;\
    http->KEY##_of=http->of;\
    http->KEY##_sz=http->sz;\
    http->of+=http->sz;\
    http->sz=0;\
    return HPE_OK;
    
#define LH_DATA_CB_SCRIPT \
    lite_http_t * http = hp->data;\
    lite_uv_handle_t * h= http->parent;\
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;\
    memcpy(&http->buf[http->of+http->sz],at,sz);\
    http->sz+=sz;\
    return HPE_OK;


// req
static int lite_http_method_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
static int lite_http_url_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
// res/res
static int lite_http_version_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
// res
static int lite_http_status_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
// req/res
static int lite_http_header_field_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
static int lite_http_header_value_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}


static int lite_http_method_complete_cb(llhttp_t * hp){LH_CB_SCRIPT(m)}
static int lite_http_url_complete_cb(llhttp_t * hp){LH_CB_SCRIPT(u)}
static int lite_http_version_complete_cb(llhttp_t * hp){LH_CB_SCRIPT(v)}
static int lite_http_status_complete_cb(llhttp_t * hp){LH_CB_SCRIPT(s)}
static int lite_http_header_field_complete_cb(llhttp_t * hp){
    lite_http_t * http = hp->data;
    lite_uv_handle_t * h= http->parent;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    http->hk_of[http->h_sz]=http->of;
    http->hk_sz[http->h_sz]=http->sz;
    http->of+=http->sz;
    http->buf[http->of++]=':';
    http->buf[http->of++]=' ';
    http->sz=0;
    return HPE_OK;
}
static int lite_http_header_value_complete_cb(llhttp_t * hp){
    lite_http_t * http = hp->data;
    lite_uv_handle_t * h= http->parent;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    http->hv_of[http->h_sz]=http->of;
    http->hv_sz[http->h_sz++]=http->sz;
    http->of+=http->sz;
    http->buf[http->of++]='\r';
    http->buf[http->of++]='\n';
    http->sz=0;
    return HPE_OK;
}

static int lite_http_headers_complete_cb(llhttp_t * hp){
    lite_http_t * http = hp->data;
    lite_uv_handle_t * h= http->parent;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary);
    lua_pushlstring(L, "headers_complete",16);
    lua_newtable(L);
    enum llhttp_type r_type=llhttp_get_type(hp);
    static const char *const r_types[] = {"request", "response", "both"};
    lua_pushlstring(L,"type",4);lua_pushstring(L, r_types[r_type]);lua_rawset(L, -3);
    lua_pushlstring(L,"method",6);lua_pushlstring(L, &http->buf[http->m_of],http->m_sz);lua_rawset(L, -3);
    lua_pushlstring(L,"url",3);lua_pushlstring(L, &http->buf[http->u_of],http->u_sz);lua_rawset(L, -3);
    lua_pushlstring(L,"http_minor",10);lua_pushinteger(L, llhttp_get_http_minor(hp));lua_rawset(L, -3);
    lua_pushlstring(L,"http_major",10);lua_pushinteger(L, llhttp_get_http_major(hp));lua_rawset(L, -3);
    lua_pushlstring(L,"version",7);lua_pushlstring(L, &http->buf[http->v_of],http->v_sz);lua_rawset(L, -3);
    lua_pushlstring(L,"status_code",11);lua_pushinteger(L, llhttp_get_status_code(hp));lua_rawset(L, -3);
    lua_pushlstring(L,"status_text",11);lua_pushlstring(L, &http->buf[http->s_of],http->s_sz);lua_rawset(L, -3);
    lua_pushlstring(L,"upgrade",7);lua_pushboolean(L, llhttp_get_upgrade(hp));lua_rawset(L, -3);
    lua_pushlstring(L,"keep_alive",10);lua_pushboolean(L, llhttp_should_keep_alive(hp));lua_rawset(L, -3);
    
    lua_pushlstring(L,"headers",7);
    lua_newtable(L);
    for(int i=0;i<http->h_sz;i++){
        lua_pushlstring(L,&http->buf[http->hk_of[i]],http->hk_sz[i]+2+http->hv_sz[i]);
        lua_rawseti(L, -2, i+1);
    
        lua_pushlstring(L,&http->buf[http->hk_of[i]],http->hk_sz[i]);
        lua_pushlstring(L,&http->buf[http->hv_of[i]],http->hv_sz[i]);   
        lua_rawset(L, -3);
    }
    lua_rawset(L, -3);
    
    return lite_http_pcall_in_cb(http,L,2);
}
static int lite_http_body_cb(llhttp_t * hp,const char* at, size_t sz){return 0;}
static int lite_http_chunk_extension_name_cb(llhttp_t * hp,const char* at, size_t sz){return 0;}
static int lite_http_chunk_extension_value_cb(llhttp_t * hp,const char* at, size_t sz){return 0;}
static int lite_http_chunk_extension_name_complete_cb(llhttp_t * hp){return 0;}
static int lite_http_chunk_extension_value_complete_cb(llhttp_t * hp){return 0;}
static int lite_http_chunk_header_cb(llhttp_t * hp){return 0;}
static int lite_http_message_complete_cb(llhttp_t * hp){
    lite_http_t * http = hp->data;
    lite_uv_handle_t * h= http->parent;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "message_complete",16);
    return lite_http_pcall_in_cb(http,L,1);
}
static int lite_http_reset_cb(llhttp_t * hp){return 0;}


static inline int  lite_ws_pcall_in_cb(lite_ws_t * ws,lua_State * L,int n){
    if (lua_pcall(L, n, 0, 0)){
        RF_SET(ws->error)
        return 1;
    }
    return WS_OK;
}

static int lite_ws_control_begin_cb(void * data,ws_frame_type_t type){
    lite_ws_t * ws=data;
    lite_uv_handle_t * h= ws->parent;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "control_begin",13);
    switch (type) {
    case WS_FRAME_TEXT:lua_pushlstring(L, "text",4);break;
    case WS_FRAME_BINARY:lua_pushlstring(L, "binary",6);break;
    case WS_FRAME_CLOSE:lua_pushlstring(L, "close",5);break;
    case WS_FRAME_PING:lua_pushlstring(L, "ping",4);break;
    case WS_FRAME_PONG:lua_pushlstring(L, "pong",4);break;
    default:lua_pushnil(L);
    }
    return lite_ws_pcall_in_cb(ws,L,2);
}
static int lite_ws_control_payload_cb(void * data,const char * at, size_t sz){
    lite_ws_t * ws=data;
    lite_uv_handle_t * h= ws->parent;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "control_payload",15);
    lua_pushlstring(L, at,sz);
    return lite_ws_pcall_in_cb(ws,L,2);
}
static int lite_ws_control_end_cb(void * data){
    lite_ws_t * ws=data;
    lite_uv_handle_t * h= ws->parent;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "control_end",11);
    return lite_ws_pcall_in_cb(ws,L,1);
}

static int lite_ws_data_begin_cb(void * data,ws_frame_type_t type){
    lite_ws_t * ws=data;
    lite_uv_handle_t * h= ws->parent;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "data_begin",10);
    switch (type) {
    case WS_FRAME_TEXT:lua_pushlstring(L, "text",4);break;
    case WS_FRAME_BINARY:lua_pushlstring(L, "binary",6);break;
    case WS_FRAME_CLOSE:lua_pushlstring(L, "close",5);break;
    case WS_FRAME_PING:lua_pushlstring(L, "ping",4);break;
    case WS_FRAME_PONG:lua_pushlstring(L, "pong",4);break;
    default:lua_pushnil(L);
    }
    return lite_ws_pcall_in_cb(ws,L,2);
}
static int lite_ws_data_payload_cb(void * data,const char * at, size_t sz){
    lite_ws_t * ws=data;
    lite_uv_handle_t * h= ws->parent;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "data_payload",12);
    lua_pushlstring(L, at,sz);
    return lite_ws_pcall_in_cb(ws,L,2);
}
static int lite_ws_data_end_cb(void * data){
    lite_ws_t * ws=data;
    lite_uv_handle_t * h= ws->parent;
    lua_State * L=((lite_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "data_end",11);
    return lite_ws_pcall_in_cb(ws,L,1);
}




/*static void lite_uv_read_http_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf){
    lite_uv_handle_t * h=stream->data;
    lite_context_t * ctx = (lite_context_t*)stream->loop->data;
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


static void lite_uv_alloc_cb(uv_handle_t* handle,size_t suggested_size,uv_buf_t* buf) {
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

static void lite_uv_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf){
    lite_uv_handle_t * h=stream->data;
    lite_context_t * ctx = (lite_context_t*)stream->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    if (nread>0){
        if (h->ws){
            h->ws->state=1;
            int rc=ws_parser_execute(&h->ws->wp,&h->ws->wc,h->ws,buf->base,nread);
            free(buf->base);
            if (RF_ISSET(h->ws->error)){
                lite_context_push_lua_error_ref(&ctx->errors, L,h->ws->error);
                h->ws->error=LUA_NOREF;
                uv_stop(&ctx->loop);
            }else if (rc && !uv_is_closing(&h->handle)){
                RF_GET(h->on_primary)
                lua_pushnil(L);
                lua_pushstring(L,ws_parser_error(rc));
                lua_pushinteger(L, rc);
                if (lua_pcall(L, 3,0,0)){
                    lite_context_push_lua_error(&ctx->errors, L);
                    uv_stop(&ctx->loop);
                }
            }
            if (h->ws->state==2){
                free(h->ws);
                h->ws=NULL;
            }
            return ;
        }
        if (h->http){
            h->http->state=1;
            int rc=llhttp_execute(&h->http->hp, buf->base,nread);
            h->http->state=0;
            free(buf->base);
            if (RF_ISSET(h->http->error)){
                lite_context_push_lua_error_ref(&ctx->errors, L,h->http->error);
                h->http->error=LUA_NOREF;
                uv_stop(&ctx->loop);
            }else if (rc!=HPE_OK && !uv_is_closing(&h->handle)){
                if ( h->http->state==1  || (h->http->state==2 && rc!=HPE_PAUSED  && rc!=HPE_PAUSED_UPGRADE && rc!=HPE_PAUSED_H2_UPGRADE) ){
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
            if (h->http->state==2){
                free(h->http);
                h->http=NULL;
            }
            return ;
        }
        lua_pushlstring(L,buf->base,nread);
        free(buf->base);
        if (lua_pcall(L, 1,0,0)){
            lite_context_push_lua_error(&ctx->errors, L);
            uv_stop(&ctx->loop);
        }
    }
    if (buf->base)
        free(buf->base);
    if (nread==0)
        return;
    lua_pushnil(L);
    lua_pushstring(L, uv_err_name(nread));
    lua_pushinteger(L, nread);
    if (lua_pcall(L, 3,0,0)){
        lite_context_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int lite_uv_stream_read_start(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    RF_UNSET_IFSET(h->on_primary)
    lua_settop(L, 2);
    RF_SET(h->on_primary)
    int rc=uv_read_start(&h->stream,lite_uv_alloc_cb,lite_uv_read_cb);
    if (rc<0) return lite_uv_err_throw(L, rc);
    return 0;
}



static int lite_uv_stream_use_ws_parser(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    if (h->http){
        if (h->http->state==1)
            h->http->state=2;
        else if (h->http->state==0){
            free(h->http);
            h->http=NULL;
        }
    }
    if (h->ws) return 0;
    h->ws=malloc(sizeof(lite_ws_t));
    if (!h->ws) return lite_uv_err_throw(L, UV_ENOMEM);
    //h->http->enabled_cbs=cbs;
    h->ws->parent=h;
    h->ws->error=LUA_NOREF;
    h->ws->state=0;
    //if (!h->http) return luaL_error(L, "required to call this from read_http_start/read_ws_start cb");
    //luaL_checktype(L, 2, LUA_TFUNCTION);
    //RF_UNSET_IFSET(h->on_primary)
    //lua_settop(L, 2);
    //RF_SET(h->on_primary)
    h->ws->wc.on_control_begin=lite_ws_control_begin_cb;
    h->ws->wc.on_control_payload=lite_ws_control_payload_cb;
    h->ws->wc.on_control_end=lite_ws_control_end_cb;

    h->ws->wc.on_data_begin=lite_ws_data_begin_cb;
    h->ws->wc.on_data_payload=lite_ws_data_payload_cb;
    h->ws->wc.on_data_end=lite_ws_data_end_cb;
    ws_parser_init(&h->ws->wp);
    return 0;
}
static int lite_uv_stream_use_http_parser(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
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
    h->http=malloc(sizeof(lite_http_t));
    if (!h->http) return lite_uv_err_throw(L, UV_ENOMEM);
    //h->http->enabled_cbs=cbs;
    h->http->parent=h;
    h->http->error=LUA_NOREF;
    h->http->state=0;
    llhttp_settings_init(&h->http->hs);
    h->http->hs.on_message_begin=lite_http_message_begin_cb;
    h->http->hs.on_url=lite_http_url_cb;
    h->http->hs.on_status=lite_http_status_cb;
    h->http->hs.on_method=lite_http_method_cb;
    h->http->hs.on_version=lite_http_version_cb;
    h->http->hs.on_header_field=lite_http_header_field_cb;
    h->http->hs.on_header_value=lite_http_header_value_cb;
    h->http->hs.on_chunk_extension_name=lite_http_chunk_extension_name_cb;
    h->http->hs.on_chunk_extension_value=lite_http_chunk_extension_value_cb;
    h->http->hs.on_headers_complete=lite_http_headers_complete_cb;
    h->http->hs.on_body=lite_http_body_cb;
    h->http->hs.on_message_complete=lite_http_message_complete_cb;
    h->http->hs.on_url_complete=lite_http_url_complete_cb;
    h->http->hs.on_status_complete=lite_http_status_complete_cb;
    h->http->hs.on_method_complete=lite_http_method_complete_cb;
    h->http->hs.on_version_complete=lite_http_version_complete_cb;
    h->http->hs.on_header_field_complete=lite_http_header_field_complete_cb;
    h->http->hs.on_header_value_complete=lite_http_header_value_complete_cb;
    h->http->hs.on_chunk_extension_name_complete=lite_http_chunk_extension_name_complete_cb;
    h->http->hs.on_chunk_extension_value_complete=lite_http_chunk_extension_value_complete_cb;
    h->http->hs.on_chunk_header=lite_http_chunk_header_cb;
    h->http->hs.on_reset=lite_http_reset_cb;
    llhttp_init(&h->http->hp, HTTP_REQUEST,&h->http->hs);
    h->http->hp.data=h->http;
    return 0;
}




static int lite_uv_stream_read_stop(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    int rc=uv_read_stop(&h->stream);//This function will always succeed
    if (h->http){
        llhttp_pause(&h->http->hp);
    }
    //A non-zero return indicates that finishing releasing resources may be pending on the next input event on that TTY on Windows, and does not indicate failure.
    lua_pushboolean(L,rc!=0);
    return 1;
}

static void lite_uv_write_cb(uv_write_t* write_req, int status) {
    lite_uv_req_t * r=write_req->data;
    uv_stream_t * stream = write_req->handle;
    lite_uv_handle_t * h=stream->data;
    lite_context_t * ctx = stream->loop->data;
    lua_State * L = ctx->L;
    int on_primary=r->on_primary;
    lite_uv_handle_unlink_req(h, r);
    free(r->buf.base);
    free(r);
    if (RF_ISSET(on_primary)){
        int n;
        RF_GET(on_primary)
        RF_UNSET(on_primary)
        if (status<0){
            lua_pushnil(L);
            lua_pushstring(L, uv_err_name(status));
            lua_pushinteger(L, status);
            n=3;
        }else{
            lua_pushboolean(L,1);
            n=1;
        }
        if (lua_pcall(L, n,0,0)){
            lite_context_push_lua_error(&ctx->errors, L);
            uv_stop(&ctx->loop);
        }
    }
}

static int lite_uv_stream_write(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    if (h->reqs_size>LITE_MAX_PENDING_WREQS){
        return luaL_error(L, "Too many pending writes! Limit is %d",LITE_MAX_PENDING_WREQS);
    }

    int f_idx=0;
    int rc;
    luaL_checktype(L, 2, LUA_TSTRING);
    if (lua_gettop(L)>2){
        luaL_checktype(L, 3, LUA_TFUNCTION);
        f_idx=3;
    }
    lite_uv_req_t * r = malloc(sizeof(lite_uv_req_t));
    if (!r) 
        goto enomem;
    size_t sz;
    const char * str=lua_tolstring(L, 2, &sz);
    if (sz==0){
        free(r);
        goto einval;
    }
    r->buf.base =malloc(sz);
    if (!r->buf.base){
        free(r);
        goto enomem;
    }
    memcpy(r->buf.base,str,sz);
    r->buf.len=sz;
    r->write.data=r;
    if (f_idx){
        lua_settop(L,f_idx);
        RF_SET(r->on_primary);
    }else{
        r->on_primary=LUA_NOREF;
    }
    lite_uv_handle_link_req_append(h, r);
    rc=uv_write(&r->write,&h->stream,&r->buf,1, lite_uv_write_cb);
    if (rc<0){
        lite_uv_handle_unlink_req(h, r);
        free(r->buf.base);
        free(r);
        goto errrc;
    }
    return 0;
einval:
    rc=UV_EINVAL;
enomem:
    rc=UV_ENOMEM;
errrc:
    return lite_uv_err_throw(L, rc);
}

static int lite_uv_stream_try_write(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    int rc=0;
    size_t sz;
    const char * str=luaL_checklstring(L, 2, &sz);
    if (!sz) return 0;
    char * p=(char *)str;
    uv_buf_t buf;
    do{ 
        p=&p[rc];
        sz-=rc;
        buf.base=p;
        buf.len=sz;
        rc=uv_try_write(&h->stream,&buf,1);
    }while(rc>-1 && rc<sz);
    if(rc<0) return lite_uv_err_throw(L, rc);
    return 0;
}


static void lite_uv_shutdown_cb(uv_shutdown_t* shutdown_req, int status) {
    lite_uv_req_t * r=shutdown_req->data;
    uv_stream_t * stream = shutdown_req->handle;
    lite_uv_handle_t * h=stream->data;
    lite_context_t * ctx = stream->loop->data;
    lua_State * L = ctx->L;
    RF_GET(r->on_primary);
    RF_UNSET(r->on_primary);
    lite_uv_handle_unlink_req(h, r);
    free(r);
    if (lua_pcall(L, 0,0,0)){
        lite_context_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int lite_uv_stream_shutdown(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lite_uv_req_t * r = malloc(sizeof(lite_uv_req_t));
    if (!r) return lite_uv_err_throw(L, UV_ENOMEM);
    r->shutdown.data=r;
    lua_settop(L, 2);
    RF_SET(r->on_primary);
    r->on_primary=LUA_NOREF;
    // r->buf.base=NULL;r->buf.len=0; this is not necessary
    lite_uv_handle_link_req_append(h, r);
    int rc= uv_shutdown(&r->shutdown,&h->stream,lite_uv_shutdown_cb);
    if (rc<0){
        lite_uv_handle_unlink_req(h, r);
        free(r);
        return lite_uv_err_throw(L, rc);
    }
    return 0;
}

static void lite_uv_stream_reg(lua_State * L){
    // handle_mt at -1
    // abstract class
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    // http req upval_mt
    /*{
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        static const luaL_Reg lite_http_req_mt[] = {
            {NULL,NULL}
        };
        luaL_setfuncs(L, lite_http_req_mt,0);

        lua_pushcclosure(L, lite_uv_stream_read_http_start,1);
        lua_setfield(L, -2, "read_http_start");
    }*/


    static const luaL_Reg lite_stream_mt[] = {
        {"listen",lite_uv_stream_listen},
        {"accept",lite_uv_stream_accept},
        {"read_start",lite_uv_stream_read_start},
        {"read_stop",lite_uv_stream_read_stop},
        {"write",lite_uv_stream_write},
        {"shutdown",lite_uv_stream_shutdown},
        {"try_write",lite_uv_stream_try_write},
        {"use_http_parser",lite_uv_stream_use_http_parser},
        {"use_ws_parser",lite_uv_stream_use_ws_parser},
        {NULL,NULL}
    };
    luaL_setfuncs(L, lite_stream_mt, 0);
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2); // setmetatable(stream_mt,handle_mt)
    lua_remove(L, -2);// pop handle_mt
    // keep stream_mt 
}

//------------------------  LITE_TCP

static int lite_tcp(lua_State * L){
    lite_context_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    lite_uv_handle_t * h=malloc(sizeof(lite_uv_handle_t));
    if (!h) return lite_uv_err_throw(L, UV_ENOMEM);
    lite_uv_handle_t ** hp=lua_newuserdata(L, sizeof(lite_uv_handle_t));
    if (!h) {
        free(h);
        return lite_uv_err_throw(L, UV_ENOMEM);
    }
    *hp=h;
    h->lua_obj_ud=hp;
    h->on_primary=LUA_NOREF;
    h->on_close=LUA_NOREF;
    h->head=NULL;
    h->tail=NULL;
    h->reqs_size=0;
    h->http=NULL;
    h->ws=NULL;
    luaL_getmetatable(L, LITE_TCP_MT);
    lua_setmetatable(L, -2);
    int rc=uv_tcp_init(&ctx->loop, &h->tcp);
    if (rc<0) return lite_uv_err_throw(L, rc);
    h->tcp.data=h;
    return 1;
}

static int lite_tcp_bind(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    const char * ip=luaL_checkstring(L, 2);
    int port=luaL_checkint(L, 3);
    int flag=lua_gettop(L)>3?luaL_checkint(L, 3):0;// UV_TCP_IPV6ONLY ?
    struct sockaddr_in bind_addr; 
    int rc=uv_ip4_addr(ip, port,&bind_addr);
    if (!rc)
        rc=uv_tcp_bind(&h->tcp, (const struct sockaddr*)&bind_addr,flag);
    if (rc<0) return lite_uv_err_throw(L, rc);
    lua_settop(L, 1);
    return 1;
}
static int lite_tcp_keepalive(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    luaL_checktype(L,2,LUA_TBOOLEAN);
    int enable=lua_toboolean(L, 2);
    int delay=0;
    if (enable){
        delay=luaL_checkinteger(L,3);
    }
    int rc=uv_tcp_keepalive(&h->tcp,enable,delay);
    if (rc<0) return lite_uv_err_throw(L, rc);
    return 0;
}


/*
static void  lite_uv_timer_cb(uv_timer_t * handle){
    lite_uv_handle_t * h=handle->data;
    lite_context_t * ctx = (lite_context_t*)handle->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    if (lua_pcall(L, 0,0,0)){
        lite_context_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int lite_tcp_connect(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    const char * ip=luaL_checkstring(L, 2);
    int port=luaL_checkint(L, 3);
    if (lua_isfunction(L, 4)){
        RF_UNSET_IFSET(h->on_primary)
        lua_settop(L, 4);
        RF_SET(h->on_primary)
    }
    if (RF_ISNOTSET(h->on_primary))
        return lite_uv_err_throw(L, UV_EINVAL);
    int rc=uv_timer_start(&h->timer, lite_uv_timer_cb,timeout,repeat);
    if (rc<0) return lite_uv_err_throw(L, rc);
    return 0;
}

static int lite_tcp_stop(lua_State * L){
    lite_uv_handle_t * h = *((lite_uv_handle_t **)lua_touserdata(L, 1));
    uv_timer_stop(&h->timer);// never fail
    return 0;
}
*/


static void lite_tcp_reg(lua_State * L){
    // stream_mt at -1
    luaL_newmetatable(L, LITE_TCP_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg lite_tcp_mt[] = {
        {"__gc",lite_uv_handle_gc},
        {"bind",lite_tcp_bind},
        {"stop",lite_timer_stop},
        {"keepalive",lite_tcp_keepalive},
        {NULL,NULL}
    };
    luaL_setfuncs(L, lite_tcp_mt, 0);
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2); // setmetatable(tcp_mt,stream_mt)
    lua_pop(L, 1);
    // keep stream_mt 
}

//--------------------------------- LITE_LOOP

static int lite_run(lua_State * L){
    lite_context_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    int mode;
    if (lua_gettop(L)){
        mode=luaL_checkinteger(L,1);
        if (mode<0 || mode>2)
            return lite_uv_err_throw(L, UV_EINVAL);
    }else
        mode=UV_RUN_DEFAULT;
    int rc=uv_run(&ctx->loop,mode);
    if (ctx->errors.len>0 || ctx->unexpected_gc_len>0){
        while (uv_loop_alive(&ctx->loop)) {
            uv_walk(&ctx->loop, lite_uv_walk_cb, NULL);
            uv_run(&ctx->loop, UV_RUN_DEFAULT);// ignore err
        }
        lua_settop(L, 0);
        if (ctx->unexpected_gc_len>0){
            lua_pushfstring(L,"%d objects were finalized before their associated C data was released (please close object)",ctx->unexpected_gc_len);
            ctx->unexpected_gc_len=0;
            rc=1;
        }else
            rc=0;
        for (int i=0;i<ctx->errors.len;i++){
            if (i || rc) lua_pushstring(L, "\n");
            if (ctx->errors.error_type[i]==LITE_ERRNO_TLUA){
                RF_GET(ctx->errors.error[i])
                RF_UNSET(ctx->errors.error[i])
            }else if (ctx->errors.error_type[i]==LITE_ERRNO_TUV){
                lua_pushfstring(L,"%s: %s",uv_err_name(ctx->errors.error[i]),uv_strerror(ctx->errors.error[i]));
            }else if (i) lua_pop(L, 1);
        }
        ctx->errors.len=0;
        lua_concat(L,lua_gettop(L));
        return lua_error(L);
    }
    if (rc!=0){
        if (mode==UV_RUN_ONCE || mode==UV_RUN_NOWAIT)
            lua_pushboolean(L, 1);
        else
            lua_pushstring(L,"unknown error occurred");
        return 1;
    }
    return 0;
}

static int lite_stop(lua_State * L){
    lite_context_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    uv_stop(&ctx->loop);
    return 0;
}


/*
static int lite_multi_socket_cb(CURL *easy, curl_socket_t s, int action, void *userp, void *socketp) {

}

static void lite_multi_timer_cb(CURLM *multi, long timeout_ms, void *userp) {
    if (timeout_ms <= 0)
        timeout_ms = 1; 
    uv_timer_start(&timeout, on_timeout, timeout_ms, 0);
}
*/


extern int luaopen_lite(lua_State * L){
    lite_uv_handle_reg(L); // handle_mt at -1
    // reg handle_mt subclasses
    lite_signal_reg(L);
    lite_timer_reg(L);
    // ... 
    lite_uv_stream_reg(L); // this will pop handle_mt and keep stream_mt
    // stream_mt at -1
    // reg stream_mt subclasses
    lite_tcp_reg(L);
    // ... 
    lua_pop(L, 1);// finally pop stream_mt 
    // lite
    lua_newtable(L);
    {
        // context
        lite_context_t * ctx=lua_newuserdata(L, sizeof(lite_context_t));
        if (!ctx) return lite_uv_err_throw(L, UV_ENOMEM);
        ctx->errors.len=0;
        ctx->L=L;
        ctx->unexpected_gc_len=0;
        {
            // context mt
            lua_createtable(L, 0,1);
            lua_pushcfunction(L, lite_context_gc);
            lua_setfield(L, -2, "__gc");
        }
        lua_setmetatable(L, -2);
        int rc=uv_loop_init(&ctx->loop);
        if (rc<0) return lite_uv_err_throw(L, rc);
        ctx->loop.data=ctx;
#ifdef LITE_CURL
        curl_global_init(CURL_GLOBAL_DEFAULT);
        ctx->curl_mhandle=curl_multi_init();
        if (!ctx->curl_mhandle) lite_uv_err_throw(L, UV_ENOMEM);
#endif
    }
    static const luaL_Reg lite_m[] = {
        {"run",lite_run},
        {"stop",lite_stop},
        {"signal",lite_signal},
        {"timer",lite_timer},
        {"tcp",lite_tcp},
        {"http_status_name",lite_http_status_name},
        {"version",lite_version},
        {NULL,NULL}
    };
    luaL_setfuncs(L, lite_m, 1);// context is upval
    return 1;
}







