

#include "lauxlib.h"
#include "lite_util.h"
#include "lua.h"
#include <stdio.h>


static void connection_cb(uv_stream_t *stream,int status){
    lite_handle_t * h=stream->data;
    lite_loop_t * ctx = (lite_loop_t*)stream->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    int n;
    if (status<0){
        lua_pushnil(L);
        lua_pushstring(L, uv_err_name(status));
        n=2;
    }else{
        lua_pushboolean(L, 1);
        n=1;
    }
    if (lua_pcall(L, n,0,0)){
        lite_loop_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}


static int stream_listen(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    int backlog=luaL_checkint(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    RF_UNSET_IFSET(h->on_primary)
    lua_settop(L, 3);
    RF_SET(h->on_primary)
    int rc=uv_listen(&h->stream,backlog,connection_cb);
    if (rc<0) return lite_uv_throw(L, rc);
    return 0;
}

static int stream_accept(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    lite_handle_t * h2 = *((lite_handle_t **)lua_touserdata(L, 2));
    int rc=uv_accept(&h->stream,&h2->stream);
    if (rc<0) return lite_uv_throw(L, rc);
    return 0;
}


static void alloc_cb(uv_handle_t* handle,size_t suggested_size,uv_buf_t* buf) {
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}


static void read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf){
    lite_handle_t * h=stream->data;
    lite_loop_t * ctx = stream->loop->data;
    lua_State * L = ctx->L;
    if (nread>0){
#ifdef LITE_WSPARSER
        if (h->ws){
            h->ws->state=1;
            int rc=ws_parser_execute(&h->ws->p,&h->ws->s,h,buf->base,nread);
            free(buf->base);
            if (RF_ISSET(h->ws->error)){
                lite_loop_push_lua_error_ref(&ctx->errors, L,h->ws->error);
                h->ws->error=LUA_NOREF;
                uv_stop(&ctx->loop);
            }else if (rc && !uv_is_closing(&h->handle)){
                RF_GET(h->on_primary)
                lua_pushnil(L);
                lua_pushstring(L,ws_parser_error(rc));
                if (lua_pcall(L, 2,0,0)){
                    lite_loop_push_lua_error(&ctx->errors, L);
                    uv_stop(&ctx->loop);
                }
            }
            if (h->ws->state==2){
                lite_ws_free(L, h->ws);
                h->ws=NULL;
            }else{
                h->ws->state=0;
            }
        }else 
#endif
#ifdef LITE_LLHTTP
        if (h->http){
            h->http->state=1;
            int rc=llhttp_execute(&h->http->p, buf->base,nread);
            free(buf->base);
            if (RF_ISSET(h->http->error)){
                lite_loop_push_lua_error_ref(&ctx->errors, L,h->http->error);
                h->http->error=LUA_NOREF;
                uv_stop(&ctx->loop);
            }else if (rc && !uv_is_closing(&h->handle)){
                if (h->http->state==1){
                    RF_GET(h->on_primary)
                    lua_pushnil(L);
                    lua_pushstring(L,llhttp_errno_name(rc));
                    if (lua_pcall(L, 2,0,0)){
                        lite_loop_push_lua_error(&ctx->errors, L);
                        uv_stop(&ctx->loop);
                    }  
                }      
            } 
            if (h->http->state==2){
                lite_http_free(L, h->http);
                h->http=NULL;
            }else{
                h->http->state=0;
            }
        }else
#endif
        {
            RF_GET(h->on_primary)
            lua_pushlstring(L, buf->base, nread);
            free(buf->base);
            if (lua_pcall(L, 1,0,0)){
                lite_loop_push_lua_error(&ctx->errors, L);
                uv_stop(&ctx->loop);
            } 
        }
            return;
    }
    if (buf->base)
        free(buf->base);
    if (nread==0)
        return;
    lua_pushnil(L);
    lua_pushstring(L, uv_err_name(nread));
    if (lua_pcall(L, 2,0,0)){
        lite_loop_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}


int lite_stream_parser(lua_State * L){
    lite_handle_t * h=*((lite_handle_t **)lua_touserdata(L, 1));
    size_t sz ;
    const char * t=luaL_checklstring(L, 2, &sz);
#ifdef LITE_WSPARSER
    if (sz==2 && t[0]=='w' && t[1]=='s'){
#ifdef LITE_LLHTTP
        if (h->http){
            if (h->http->state==1)
                h->http->state=2;
            else if (h->http->state==0){
                lite_http_free(L, h->http);
                h->http=NULL;
            }
        } 
#endif   
        return lite_ws_use_parser(L,h);
    }
#endif
#ifdef LITE_LLHTTP
    if (sz==4 && t[0]=='h' && t[1]=='t' && t[2]=='t' && t[3]=='p'){
#ifdef LITE_WSPARSER
        if (h->ws){
            if (h->ws->state==1)
                h->ws->state=2;
            else if (h->ws->state==0){
                lite_ws_free(L, h->ws);
                h->ws=NULL;
            }
        }
#endif
        return lite_http_use_parser(L,h);
    }
#endif
    return luaL_error(L, "unsupported parser");
}



static int stream_read_start(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    RF_UNSET_IFSET(h->on_primary)
    lua_settop(L, 2);
    RF_SET(h->on_primary)
    int rc=uv_read_start(&h->stream,alloc_cb,read_cb);
    if (rc<0) return lite_uv_throw(L, rc);
    return 0;
}

static int stream_read_stop(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    int rc=uv_read_stop(&h->stream);//This function will always succeed
    //A non-zero return indicates that finishing releasing resources may be pending on the next input event on that TTY on Windows, and does not indicate failure.
    lua_pushboolean(L,rc!=0);
    return 1;
}

static void write_cb(uv_write_t* write_req, int status) {
    lite_req_t * r=write_req->data;
    uv_stream_t * stream = write_req->handle;
    lite_handle_t * h=stream->data;
    lite_loop_t * ctx = stream->loop->data;
    lua_State * L = ctx->L;
    int on_primary=r->on_primary;
    lite_handle_unlink_req(h, r);
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
            lite_loop_push_lua_error(&ctx->errors, L);
            uv_stop(&ctx->loop);
        }
    }
}


static int stream_write(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
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
    lite_req_t * r = malloc(sizeof(lite_req_t));
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
    lite_handle_link_req_append(h, r);
    rc=uv_write(&r->write,&h->stream,&r->buf,1, write_cb);
    if (rc<0){
        lite_handle_unlink_req(h, r);
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
    return lite_uv_throw(L, rc);
}


static int stream_try_write(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
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
    if(rc<0) return lite_uv_throw(L, rc);
    return 0;
}

static void shutdown_cb(uv_shutdown_t* shutdown_req, int status) {
    lite_req_t * r=shutdown_req->data;
    uv_stream_t * stream = shutdown_req->handle;
    lite_handle_t * h=stream->data;
    lite_loop_t * ctx = stream->loop->data;
    lua_State * L = ctx->L;
    RF_GET(r->on_primary);
    RF_UNSET(r->on_primary);
    lite_handle_unlink_req(h, r);
    free(r);
    if (lua_pcall(L, 0,0,0)){
        lite_loop_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int stream_shutdown(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lite_req_t * r = malloc(sizeof(lite_req_t));
    if (!r) return lite_uv_throw(L, UV_ENOMEM);
    r->shutdown.data=r;
    lua_settop(L, 2);
    RF_SET(r->on_primary);
    //r->on_primary=LUA_NOREF;
    // r->buf.base=NULL;r->buf.len=0; this is not necessary
    lite_handle_link_req_append(h, r);
    int rc= uv_shutdown(&r->shutdown,&h->stream,shutdown_cb);
    if (rc<0){
        lite_handle_unlink_req(h, r);
        free(r);
        return lite_uv_throw(L, rc);
    }
    return 0;
}


void lite_stream_reg(lua_State * L){
    luaL_newmetatable(L,LITE_STREAM_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg stream_mt[] = {
        {"listen",stream_listen},
        {"accept",stream_accept},
        {"read_start",stream_read_start},
        {"read_stop",stream_read_stop},
        {"write",stream_write},
        {"shutdown",stream_shutdown},
        {"try_write",stream_try_write},
        {"parser",lite_stream_parser},
        //{"use_ws_parser",M_stream_use_ws_parser},
        {NULL,NULL}
    };
    luaL_setfuncs(L, stream_mt, 0);
    luaL_getmetatable(L, LITE_HANDLE_MT);
    lua_setmetatable(L, -2); // setmetatable(stream_mt,handle_mt)
    lua_pop(L, 1);
}