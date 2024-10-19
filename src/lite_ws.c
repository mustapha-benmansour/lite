#include "lauxlib.h"
#ifdef LITE_WSPARSER



#include "lite_util.h"
#include "lua.h"


void lite_ws_read(char *base,ssize_t nread){
    
}


void lite_ws_free(lua_State * L,lite_ws_t *ws){
    if (ws){
        RF_UNSET_IFSET(ws->error)
        free(ws);
    }
}



static inline int  M_ws_pcall_in_cb(lite_ws_t * ws,lua_State * L,int n){
    if (lua_pcall(L, n, 0, 0)){
        RF_SET(ws->error)
        return 1;
    }
    return WS_OK;
}

static int M_ws_control_begin_cb(void * data,ws_frame_type_t type){
    lite_handle_t * h=data;
    lite_ws_t * ws =h->ws;
    ws->sz=0;
    ws->of=0;
    ws->tp=type;
    return WS_OK;
    /*M_handle_t * h= ws->parent;
    lua_State * L=((M_context_t * )h->handle.loop->data)->L;
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
    return M_ws_pcall_in_cb(ws,L,2);*/
}
static int M_ws_control_payload_cb(void * data,const char * at, size_t sz){
    lite_handle_t * h=data;
    lite_ws_t * ws =h->ws;
    memcpy(&ws->buf[ws->of+ws->sz],at,sz);
    ws->sz+=sz;
    return WS_OK;
    /*M_handle_t * h= ws->parent;
    lua_State * L=((M_context_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "control_payload",15);
    lua_pushlstring(L, at,sz);
    return M_ws_pcall_in_cb(ws,L,2);*/
}
static int M_ws_control_end_cb(void * data){
    lite_handle_t * h=data;
    lite_ws_t * ws =h->ws;
    lua_State * L=((lite_loop_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "control",7);
    switch (ws->tp) {
    case WS_FRAME_CLOSE:lua_pushlstring(L, "close",5);break;
    case WS_FRAME_PING:lua_pushlstring(L, "ping",4);break;
    case WS_FRAME_PONG:lua_pushlstring(L, "pong",4);break;
    default:lua_pushnil(L);
    }
    lua_pushlstring(L, &ws->buf[ws->of],ws->sz);
    return M_ws_pcall_in_cb(ws,L,3);
}

static int M_ws_data_begin_cb(void * data,ws_frame_type_t type){
    lite_handle_t * h=data;
    lite_ws_t * ws =h->ws;
    lua_State * L=((lite_loop_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "data_begin",10);
    switch (type) {
    case WS_FRAME_TEXT:lua_pushlstring(L, "text",4);break;
    case WS_FRAME_BINARY:lua_pushlstring(L, "binary",6);break;
    default:lua_pushnil(L);
    }
    return M_ws_pcall_in_cb(ws,L,2);
}
static int M_ws_data_payload_cb(void * data,const char * at, size_t sz){
    lite_handle_t * h=data;
    lite_ws_t * ws =h->ws;
    lua_State * L=((lite_loop_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "data_payload",12);
    lua_pushlstring(L, at,sz);
    return M_ws_pcall_in_cb(ws,L,2);
}
static int M_ws_data_end_cb(void * data){
    lite_handle_t * h=data;
    lite_ws_t * ws =h->ws;
    lua_State * L=((lite_loop_t * )h->handle.loop->data)->L;
    RF_GET(h->on_primary)
    lua_pushlstring(L, "data_end",8);
    return M_ws_pcall_in_cb(ws,L,1);
}



int lite_ws_use_parser(lua_State * L,lite_handle_t * h){
    if (h->ws) return 0;
    h->ws=malloc(sizeof(lite_ws_t));
    if (!h->ws) return lite_uv_throw(L, UV_ENOMEM);
    //h->http->enabled_cbs=cbs;
    h->ws->error=LUA_NOREF;
    h->ws->state=0;
    //if (!h->http) return luaL_error(L, "required to call this from read_http_start/read_ws_start cb");
    //luaL_checktype(L, 2, LUA_TFUNCTION);
    //RF_UNSET_IFSET(h->on_primary)
    //lua_settop(L, 2);
    //RF_SET(h->on_primary)
    h->ws->s.on_control_begin=M_ws_control_begin_cb;
    h->ws->s.on_control_payload=M_ws_control_payload_cb;
    h->ws->s.on_control_end=M_ws_control_end_cb;

    h->ws->s.on_data_begin=M_ws_data_begin_cb;
    h->ws->s.on_data_payload=M_ws_data_payload_cb;
    h->ws->s.on_data_end=M_ws_data_end_cb;
    ws_parser_init(&h->ws->p);
    return 0;
}



int lite_ws_build_frame(lua_State * L){
    if (lua_type(L, 1)!=LUA_TSTRING)
        return lite_error_invalid_arg(L);
    size_t message_len;
    const char * message=luaL_checklstring(L, 1, &message_len);
    int is_bin=lua_toboolean(L, 2);
    uint8_t fin = 0x80;  // FIN bit set to indicate final frame
    uint8_t opcode = is_bin?0x02:0x01;  // Opcode for text frame
    size_t frame_size = 2;  // Initial size for FIN+Opcode, Payload length

    if (message_len <= 125) {
        frame_size += message_len;  // Simple case, fits within 1 byte for length
    } else if (message_len <= 65535) {
        frame_size += 2 + message_len;  // Extended payload length (16 bits)
    } else {
        frame_size += 8 + message_len;  // Extended payload length (64 bits)
    }

    // Allocate memory for the frame
    uint8_t *frame = (uint8_t *)malloc(frame_size);
    if (!frame) {
        perror("Failed to allocate memory for WebSocket frame");
        exit(EXIT_FAILURE);
    }

    // First byte: FIN + Opcode
    frame[0] = fin | opcode;

    // Second byte: Payload length
    if (message_len <= 125) {
        frame[1] = (uint8_t)message_len;  // No mask bit, no extended length
        memcpy(&frame[2], message, message_len);  // Copy the message directly
    } else if (message_len <= 65535) {
        frame[1] = 126;  // 16-bit extended payload length
        frame[2] = (message_len >> 8) & 0xFF;  // High byte of length
        frame[3] = message_len & 0xFF;  // Low byte of length
        memcpy(&frame[4], message, message_len);  // Copy the message directly
    } else {
        frame[1] = 127;  // 64-bit extended payload length
        for (int i = 0; i < 8; i++) {
            frame[9 - i] = (message_len >> (i * 8)) & 0xFF;
        }
        memcpy(&frame[10], message, message_len);  // Copy the message directly
    }

    // Send the frame to the client
    lua_pushlstring(L, (char *)frame, frame_size);
    // Free the allocated frame
    free(frame);
    return 1;
}

#endif