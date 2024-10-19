

#include "lite_util.h"
#include "lua.h"

static int version(lua_State * L){
    lua_pushfstring(L,
    "lite/v%d.%d.%d libuv/v%d.%d.%d",// llhttp/v%d.%d.%d",
    LITE_VERSION_MAJOR,LITE_VERSION_MINOR,LITE_VERSION_PATCH,
    UV_VERSION_MAJOR,UV_VERSION_MINOR,UV_VERSION_PATCH);//,
    //LLHTTP_VERSION_MAJOR,LLHTTP_VERSION_MINOR,LLHTTP_VERSION_PATCH);
    return 1;
}


int lite_error_nomem(lua_State * L){
    lua_pushnil(L);
    lua_pushliteral(L, "ENOMEM");
    return 2;
}
int lite_error_nomem_throw(lua_State * L){
    lua_pushliteral(L, "ENOMEM");
    return lua_error(L);
}


extern int luaopen_lite(lua_State * L){

#ifdef LITE_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    lite_easy_reg(L);
#endif
    lite_handle_reg(L);
        lite_signal_reg(L);
        lite_timer_reg(L);
        lite_stream_reg(L);
            lite_tcp_reg(L);
    lua_newtable(L);
    {
        // state_loop
        lite_loop_t * ctx=lua_newuserdata(L, sizeof(lite_loop_t));
        if (!ctx) return lite_error_nomem_throw(ctx->L);
        ctx->errors.len=0;
        ctx->L=L;
        ctx->unexpected_gc_len=0;
        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, lite_loop_gc);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);
        int rc=uv_loop_init(&ctx->loop);
        if (rc<0) return lite_uv_throw(L, rc);
        ctx->loop.data=ctx;
#ifdef LITE_CURL
        lite_multi_init(ctx);
#endif
        static const luaL_Reg m_up[] = {
                {"run",lite_loop_run},
                {"stop",lite_loop_stop},

                {"timer",lite_timer},
                {"signal",lite_signal},
                {"tcp",lite_tcp},
#ifdef LITE_CURL
                {"easy",lite_easy},
#endif
                {NULL,NULL}
        };
        luaL_setfuncs(L, m_up, 1);// state_loop is upval
    }
    static const luaL_Reg m[] = {
        //{"http_status_name",M_http_status_name},
        {"version",version},
        {"http_status_name",lite_http_status_name},
#ifdef LITE_WSPARSER
        {"ws_build_frame",lite_ws_build_frame},
#endif
        {NULL,NULL}
    };
    luaL_setfuncs(L, m, 0);// context is upval
#ifdef LITE_CJSON
    luaopen_cjson_safe(L);
    lua_setfield(L, -2, "json");
#endif
    return 1;
}

