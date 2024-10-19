
#include "lite_util.h"
#include "lua.h"
#define LITE_TIMER_MT "lite.timer"


int lite_timer(lua_State * L){
    lite_loop_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    lite_handle_t ** hp=lite_handle(L);
    if (!hp) return lite_error_nomem(L);
    luaL_getmetatable(L, LITE_TIMER_MT);
    lua_setmetatable(L, -2);
    int rc=uv_timer_init(&ctx->loop, &(*hp)->timer);
    if (rc<0) return lite_uv_throw(L, rc);
    return 1;
}


static void  timer_cb(uv_timer_t * handle){
    lite_handle_t * h=handle->data;
    lite_loop_t * ctx = (lite_loop_t*)handle->loop->data;
    lua_State * L = ctx->L;
    RF_GET(h->on_primary)
    if (lua_pcall(L, 0,0,0)){
        lite_loop_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}

static int timer_start(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    int top=lua_gettop(L);
    if (lua_type(L, 3)!=LUA_TNUMBER || (top>3 && lua_type(L, 4)!=LUA_TNUMBER))
        return lite_error_invalid_arg(L);
    int timeout=lua_tointeger(L, 3);
    int repeat=top>3 ? lua_tointeger(L,4):0;
    if (!lua_isfunction(L, 2))
        return lite_error_invalid_arg(L);
    RF_UNSET_IFSET(h->on_primary)
    lua_settop(L, 2);
    RF_SET(h->on_primary)
    int rc=uv_timer_start(&h->timer, timer_cb,timeout,repeat);
    if (rc<0) return lite_uv_throw(L, rc);
    return lite_success(L);
}

static int timer_stop(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    int rc=uv_timer_stop(&h->timer);// never fail
    if (rc<0) return lite_uv_throw(L, rc);
    return lite_success(L);
}



void lite_timer_reg(lua_State * L){
    luaL_newmetatable(L, LITE_TIMER_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg timer_mt[] = {
        {"__gc",lite_handle_gc},
        {"start",timer_start},
        {"stop",timer_stop},
        {NULL,NULL}
    };
    luaL_setfuncs(L, timer_mt, 0);
    luaL_getmetatable(L, LITE_HANDLE_MT);
    lua_setmetatable(L, -2); // setmetatable(timer_mt,handle_mt)
    lua_pop(L, 1);
}