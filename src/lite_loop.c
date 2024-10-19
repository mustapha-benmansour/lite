



#include "lite_util.h"
#include "lua.h"
#include <stdio.h>
#include <uv.h>

#define LITE_LOOP_MT "lite.loop"


void lite_loop_push_lua_error(lite_async_errs_t * errors,lua_State * L){
    int len=errors->len;
    if (len<10){
        RF_SET(errors->err[len])
        errors->err_type[len]=LITE_ASYNC_ERR_TLUA;
        errors->len++;
    }else
        lua_pop(L, 1);
}

void lite_loop_push_lua_error_ref(lite_async_errs_t * errors,lua_State * L,int ref){
    int len=errors->len;
    if (len<10){
        errors->err[len]=ref;
        errors->err_type[len]=LITE_ASYNC_ERR_TLUA;
        errors->len++;
    }else
        RF_UNSET(ref)
}

static void walk_cb(uv_handle_t *handle, void *arg){
    (void)arg;
    if (!uv_is_closing(handle))
        uv_close(handle, lite_handle_close_cb);
}

int lite_loop_gc(lua_State * L){
    lite_loop_t * ctx=lua_touserdata(L, 1);
    lite_multi_clean(ctx);
    while (uv_loop_close(&ctx->loop)) {
        uv_walk(&ctx->loop, walk_cb, NULL);
        uv_run(&ctx->loop, UV_RUN_DEFAULT);// ignore err
    }
    return 0;
}


int lite_loop_run(lua_State * L){
    lite_loop_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    int mode;
    if (lua_gettop(L)){
        if (lua_type(L, 1)!=LUA_TNUMBER)
            return lite_error_invalid_arg(L);
        mode=lua_tointeger(L,1);
        if (mode<0 || mode>2)
            return lite_error_invalid_arg(L);
    }else
        mode=UV_RUN_DEFAULT;
    int rc=uv_run(&ctx->loop,mode);
    if (ctx->errors.len>0 || ctx->unexpected_gc_len>0){
        while (uv_loop_alive(&ctx->loop)) {
            uv_walk(&ctx->loop, walk_cb, NULL);
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
            if (ctx->errors.err_type[i]==LITE_ASYNC_ERR_TLUA){
                RF_GET(ctx->errors.err[i])
                RF_UNSET(ctx->errors.err[i])
            }else if (ctx->errors.err_type[i]==LITE_ASYNC_ERR_TUV){
                lua_pushfstring(L,"%s: %s",uv_err_name(ctx->errors.err[i]),uv_strerror(ctx->errors.err[i]));
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


int lite_loop_stop(lua_State * L){
    lite_loop_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    uv_stop(&ctx->loop);
    return 0;
}


