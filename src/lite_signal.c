



#include "lauxlib.h"
#include "lite_util.h"
#include "lua.h"

#define LITE_SIGNAL_MT "lite.signal"

int lite_signal(lua_State * L){
    lite_loop_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    lite_handle_t ** hp=lite_handle(L);
    if (!hp) return lite_error_nomem(L);
    luaL_getmetatable(L, LITE_SIGNAL_MT);
    lua_setmetatable(L, -2);
    int rc=uv_signal_init(&ctx->loop, &(*hp)->signal);
    if (rc<0) return lite_uv_throw(L, rc);
    return 1;
}

static void  signal_cb(uv_signal_t * handle,int signum){
    lite_handle_t * h=handle->data;
    lite_loop_t * ctx = (lite_loop_t*)handle->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    lua_pushinteger(L, signum);
    if (lua_pcall(L, 1,0,0)){
        lite_loop_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int signal_start(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    if (lua_type(L, 2)!=LUA_TNUMBER)
        return lite_error_invalid_arg(L);
    int signum=lua_tointeger(L, 2);
    if (!lua_isfunction(L, 3))
        return lite_error_invalid_arg(L);
    RF_UNSET_IFSET(h->on_primary)
    lua_settop(L, 3);
    RF_SET(h->on_primary)
    int rc=uv_signal_start(&h->signal,signal_cb,signum);
    if (rc<0) return lite_uv_throw(L, rc);
    return lite_success(L);
}

static int signal_stop(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    int rc=uv_signal_stop(&h->signal);// never fail
    if (rc<0) return lite_uv_throw(L, rc);
    return lite_success(L);
}


void lite_signal_reg(lua_State * L){
    luaL_newmetatable(L, LITE_SIGNAL_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg signal_mt[] = {
        {"__gc",lite_handle_gc},
        {"start",signal_start},
        {"stop",signal_stop},
        {NULL,NULL}
    };
    luaL_setfuncs(L, signal_mt, 0);
    luaL_getmetatable(L, LITE_HANDLE_MT);
    lua_setmetatable(L, -2); // setmetatable(signal_mt,handle_mt)
    lua_pop(L, 1);
}


