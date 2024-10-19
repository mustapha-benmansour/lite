
#include "lauxlib.h"
#include "lite_util.h"
#include "lua.h"


#define LITE_TCP_MT "lite.tcp"



int lite_tcp(lua_State * L){
    lite_loop_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    lite_handle_t ** hp=lite_handle(L);
    if (!hp) return lite_error_nomem(L);
    luaL_getmetatable(L, LITE_TCP_MT);
    lua_setmetatable(L, -2);
    int rc=uv_tcp_init(&ctx->loop, &(*hp)->tcp);
    if (rc<0) return lite_uv_throw(L, rc);
    return 1;
}

static int tcp_bind(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    int top=lua_gettop(L);
    if (lua_type(L, 2)!=LUA_TSTRING || lua_type(L,3)!=LUA_TNUMBER || (top>3 && lua_type(L, 4)!=LUA_TNUMBER))
        return lite_error_invalid_arg(L);
    const char * ip=lua_tostring(L, 2);
    int port=lua_tointeger(L, 3);
    int flag=top>3?lua_tointeger(L, 4):0;// UV_TCP_IPV6ONLY ?
    struct sockaddr_in bind_addr; 
    int rc=uv_ip4_addr(ip, port,&bind_addr);
    if (!rc)
        rc=uv_tcp_bind(&h->tcp, (const struct sockaddr*)&bind_addr,flag);
    if (rc<0) return lite_uv_throw(L, rc);
    return lite_success(L);
}

static int tcp_keepalive(lua_State * L){
    lite_handle_t * h = *((lite_handle_t **)lua_touserdata(L, 1));
    if (!lua_isboolean(L, 2))
        return lite_error_invalid_arg(L);
    int enable=lua_toboolean(L, 2);
    int delay=0;
    if (enable){
        if (lua_type(L, 3)!=LUA_TNUMBER)
            return lite_error_invalid_arg(L);
        delay=lua_tointeger(L,3);
    }
    int rc=uv_tcp_keepalive(&h->tcp,enable,delay);
    if (rc<0) return lite_uv_throw(L, rc);
    return lite_success(L);
}

void lite_tcp_reg(lua_State * L){
    luaL_newmetatable(L, LITE_TCP_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg tcp_mt[] = {
        {"__gc",lite_handle_gc},
        {"bind",tcp_bind},
        {"keepalive",tcp_keepalive},
        {NULL,NULL}
    };
    luaL_setfuncs(L, tcp_mt, 0);
    luaL_getmetatable(L, LITE_STREAM_MT);
    lua_setmetatable(L, -2); // setmetatable(tcp_mt,stream_mt)
    lua_pop(L, 1);
}