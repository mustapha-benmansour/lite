

#include "lite_util.h"


void lite_handle_link_req_append(lite_handle_t * h,lite_req_t * r){
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
void lite_handle_unlink_req(lite_handle_t * h,lite_req_t * r){
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


void lite_handle_close_cb(uv_handle_t * handle){
    lite_handle_t * h=handle->data;
    lite_loop_t * ctx = (lite_loop_t*)handle->loop->data;
    lua_State * L = ctx->L;
    RF_UNSET(h->on_primary)
    if (RF_ISSET(h->on_close)){
        RF_GET(h->on_close)
        RF_UNSET(h->on_close)
        if (lua_pcall(L, 0,0,0))
            lite_loop_push_lua_error(&ctx->errors,L);
    }
    lite_http_free(L,h->http);
    lite_ws_free(L, h->ws);
    // a pending reqs related to this handle still there ?
    // not supposed to happen (libuv will call cb with ECANCEL ?????) note: freeing req is already implemented in cb
    if (h->head){
        // libuv bug ??? 
        // ok free reqs anyway (this not our problem)
        // may cause segmentation fault
        while (h->head) {
            lite_req_t * r=h->head;
            lite_handle_unlink_req(h, r);
            RF_UNSET(r->on_primary)
            free(r);
        }       
    }
    if (*h->lua_obj_ud)
        *h->lua_obj_ud=NULL;
    free(h);
}

int lite_handle_gc(lua_State * L){
    lite_handle_t ** hp = lua_touserdata(L, 1);
    if (*hp){
        lite_loop_t * ctx = (*hp)->handle.loop->data;
        ctx->unexpected_gc_len++;
        uv_stop(&ctx->loop);
    }
    return 0;
}

static int handle_on_close(lua_State * L){
    lite_handle_t ** hp = lua_touserdata(L, 1);
    luaL_checktype(L, 2,LUA_TFUNCTION);
    RF_UNSET_IFSET((*hp)->on_close)
    lua_settop(L, 2);
    RF_SET((*hp)->on_close);
    return 0;
}

static int handle_close(lua_State * L){
    lite_handle_t ** hp = lua_touserdata(L, 1);
    lua_settop(L, 1);
    lua_pushnil(L);
    lua_setmetatable(L, -2);
    if (!uv_is_closing(&(*hp)->handle))
        uv_close(&(*hp)->handle, lite_handle_close_cb);
    *hp=NULL;
    return 0;
}


lite_handle_t ** lite_handle(lua_State * L){
    lite_handle_t * h=malloc(sizeof(lite_handle_t));
    if (!h) return NULL;
    lite_handle_t ** hp=lua_newuserdata(L, sizeof(lite_handle_t));
    if (!h) {
        free(h);
        return NULL;
    }
    *hp=h;
    h->lua_obj_ud=hp;
    h->on_primary=LUA_NOREF;
    h->on_close=LUA_NOREF;
    h->head=NULL;
    h->tail=NULL;
    h->reqs_size=0;
    h->handle.data=h;
    h->http=NULL;
    h->ws=NULL;
    return hp;
}


void lite_handle_reg(lua_State * L){
    // abstract class
    luaL_newmetatable(L,LITE_HANDLE_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg handle_mt[] = {
        {"close",handle_close},
        {"on_close",handle_on_close},
        {NULL,NULL}
    };
    luaL_setfuncs(L, handle_mt, 0);
    lua_pop(L, 1);
}

