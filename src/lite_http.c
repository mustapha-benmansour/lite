
#ifdef LITE_LLHTTP

#include "lauxlib.h"
#include "lite_util.h"
#include "lua.h"
#include <llhttp.h>


#define LITE_HTTP_MT "lite.http"

int lite_http_status_name(lua_State * L){
    if (lua_type(L,1)!=LUA_TNUMBER)
        return lite_error_invalid_arg(L);
    int status=lua_tointeger(L, 1);
    lua_pushstring(L, llhttp_status_name(status));
    return 1;
}


void lite_http_free(lua_State * L,lite_http_t *http){
    if (http){
        RF_UNSET_IFSET(http->error)
        free(http);
    }
}



static inline int  M_http_pcall_in_cb(lite_http_t * http,lua_State * L,int n){
    if (lua_pcall(L, n, 0, 0)){
        RF_SET(http->error)
        return HPE_USER;
    }
    return HPE_OK;
}


static int M_http_message_begin_cb(llhttp_t * hp){
    lite_handle_t * h= hp->data;\
    lite_http_t * http = h->http;\
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    http->of=0;
    http->sz=0;
    http->m_sz=0;
    http->u_sz=0;
    http->v_sz=0;
    http->s_sz=0;
    http->h_sz=0;
    lua_State * L=((lite_loop_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "message_begin",13);
    return M_http_pcall_in_cb(http,L,1);
}

#define LH_CB_SCRIPT(KEY) \
    lite_handle_t * h= hp->data;\
    lite_http_t * http = h->http;\
    http->KEY##_of=http->of;\
    http->KEY##_sz=http->sz;\
    http->of+=http->sz;\
    http->sz=0;\
    return HPE_OK;
    
#define LH_DATA_CB_SCRIPT \
    lite_handle_t * h= hp->data;\
    lite_http_t * http = h->http;\
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;\
    memcpy(&http->buf[http->of+http->sz],at,sz);\
    http->sz+=sz;\
    return HPE_OK;


// req
static int M_http_method_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
static int M_http_url_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
// res/res
static int M_http_version_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
// res
static int M_http_status_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
// req/res
static int M_http_header_field_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}
static int M_http_header_value_cb(llhttp_t * hp,const char* at, size_t sz){LH_DATA_CB_SCRIPT}


static int M_http_method_complete_cb(llhttp_t * hp){LH_CB_SCRIPT(m)}
static int M_http_url_complete_cb(llhttp_t * hp){LH_CB_SCRIPT(u)}
static int M_http_version_complete_cb(llhttp_t * hp){LH_CB_SCRIPT(v)}
static int M_http_status_complete_cb(llhttp_t * hp){LH_CB_SCRIPT(s)}
static int M_http_header_field_complete_cb(llhttp_t * hp){
    lite_handle_t * h= hp->data;
    lite_http_t * http = h->http;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    http->hk_of[http->h_sz]=http->of;
    http->hk_sz[http->h_sz]=http->sz;
    http->of+=http->sz;
    http->buf[http->of++]=':';
    http->buf[http->of++]=' ';
    http->sz=0;
    return HPE_OK;
}
static int M_http_header_value_complete_cb(llhttp_t * hp){
    lite_handle_t * h= hp->data;
    lite_http_t * http = h->http;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    http->hv_of[http->h_sz]=http->of;
    http->hv_sz[http->h_sz++]=http->sz;
    http->of+=http->sz;
    http->buf[http->of++]='\r';
    http->buf[http->of++]='\n';
    http->sz=0;
    return HPE_OK;
}

static int M_http_headers_complete_cb(llhttp_t * hp){
    lite_handle_t * h= hp->data;
    lite_http_t * http = h->http;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    lua_State * L=((lite_loop_t * )h->handle.loop->data)->L;
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
    
    return M_http_pcall_in_cb(http,L,2);
}
static int M_http_body_cb(llhttp_t * hp,const char* at, size_t sz){return 0;}
static int M_http_chunk_extension_name_cb(llhttp_t * hp,const char* at, size_t sz){return 0;}
static int M_http_chunk_extension_value_cb(llhttp_t * hp,const char* at, size_t sz){return 0;}
static int M_http_chunk_extension_name_complete_cb(llhttp_t * hp){return 0;}
static int M_http_chunk_extension_value_complete_cb(llhttp_t * hp){return 0;}
static int M_http_chunk_header_cb(llhttp_t * hp){return 0;}
static int M_http_message_complete_cb(llhttp_t * hp){
    lite_handle_t * h= hp->data;
    lite_http_t * http = h->http;
    if (http->state==2 || uv_is_closing(&h->handle)) return HPE_PAUSED;
    lua_State * L=((lite_loop_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "message_complete",16);
    return M_http_pcall_in_cb(http,L,1);
}
static int M_http_reset_cb(llhttp_t * hp){return 0;}





int lite_http_use_parser(lua_State * L,lite_handle_t * h){
    if (h->http) return 0;
    h->http=malloc(sizeof(lite_http_t));
    if (!h->http) return lite_error_nomem(L);
    h->http->state=0;
    h->http->error=LUA_NOREF;
    llhttp_settings_init(&h->http->s);
    h->http->s.on_message_begin=M_http_message_begin_cb;
    h->http->s.on_url=M_http_url_cb;
    h->http->s.on_status=M_http_status_cb;
    h->http->s.on_method=M_http_method_cb;
    h->http->s.on_version=M_http_version_cb;
    h->http->s.on_header_field=M_http_header_field_cb;
    h->http->s.on_header_value=M_http_header_value_cb;
    h->http->s.on_chunk_extension_name=M_http_chunk_extension_name_cb;
    h->http->s.on_chunk_extension_value=M_http_chunk_extension_value_cb;
    h->http->s.on_headers_complete=M_http_headers_complete_cb;
    h->http->s.on_body=M_http_body_cb;
    h->http->s.on_message_complete=M_http_message_complete_cb;
    h->http->s.on_url_complete=M_http_url_complete_cb;
    h->http->s.on_status_complete=M_http_status_complete_cb;
    h->http->s.on_method_complete=M_http_method_complete_cb;
    h->http->s.on_version_complete=M_http_version_complete_cb;
    h->http->s.on_header_field_complete=M_http_header_field_complete_cb;
    h->http->s.on_header_value_complete=M_http_header_value_complete_cb;
    h->http->s.on_chunk_extension_name_complete=M_http_chunk_extension_name_complete_cb;
    h->http->s.on_chunk_extension_value_complete=M_http_chunk_extension_value_complete_cb;
    h->http->s.on_chunk_header=M_http_chunk_header_cb;
    h->http->s.on_reset=M_http_reset_cb;
    llhttp_init(&h->http->p, HTTP_REQUEST,&h->http->s);
    h->http->p.data=h;
    return lite_success(L);
}


#endif