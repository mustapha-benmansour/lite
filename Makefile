CC=gcc
PREFIX=/usr/local
LUA_VERSION = 5.1
LUA_LIBDIR= $(PREFIX)/lib/lua/$(LUA_VERSION)
LUA_INC += -I$(PREFIX)/include -I$(PREFIX)/include/lua$(LUA_VERSION) -I$(PREFIX)/include/lua/$(LUA_VERSION) 
LUA_INC += -I/usr/include -I/usr/include/lua$(LUA_VERSION) -I/usr/include/lua/$(LUA_VERSION) 
OPT= -shared 
CFLAGS= -g -O0 -Wall  -fPIC $(LUA_INC)
LDFLAGS=

Z_CFLAGS=#-I/usr/local/include
Z_LDFLAGS=#-L/usr/local/lib -lz

UV_CFLAGS=-I/usr/local/include
UV_LDFLAGS=-L/usr/local/lib -luv 

LLHTTP_CFLAGS=-I/usr/local/include
LLHTTP_LDFLAGS=-L/usr/local/lib -lllhttp 

LITE_LLHTTP ?= y
LITE_WSPARSER ?= y
LITE_CJSON ?= y

LITE_CURL ?= y



SRCS = src/lite_util.c  src/lite.c src/lite_loop.c src/lite_handle.c src/lite_timer.c src/lite_signal.c src/lite_stream.c src/lite_tcp.c

ifeq ($(LITE_LLHTTP),y)
    CFLAGS += -DLITE_LLHTTP
    SRCS += src/lite_http.c deps/llhttp/src/llhttp.c deps/llhttp/src/http.c deps/llhttp/src/api.c
endif

ifeq ($(LITE_WSPARSER),y)
    CFLAGS += -DLITE_WSPARSER
    SRCS += src/lite_ws.c deps/ws_parser/ws_parser.c
endif

ifeq ($(LITE_CJSON),y)
    CFLAGS += -DLITE_CJSON
    SRCS += deps/lua-cjson/lua_cjson.c 
	SRCS += deps/lua-cjson/strbuf.c
	SRCS += deps/lua-cjson/fpconv.c
endif

ifeq ($(LITE_CURL),y)
    #CFLAGS += -I/usr/local/url/
	CFLAGS += -DLITE_CURL
    LDFLAGS += -L/usr/local/lib -lcurl
	SRCS += src/lite_easy.c src/lite_multi.c
endif





all: lite.so


lite.so: $(SRCS)
	@echo CC $@
	@$(CC) $(OPT)  $(CFLAGS) -DLITE_MEM -DLITE_HTTP  $(LLHTTP_CFLAGS) $(UV_CFLAGS) -o $@ $^ $(UV_LDFLAGS) $(LDFLAGS) 

sha1.so: sha1.c
	@echo CC $@
	@$(CC) $(OPT)  $(CFLAGS) -o $@ $^ 


test: lite.so sha1.so
#LD_LIBRARY_PATH=/usr/local/lib/ luajit -v 
	LD_LIBRARY_PATH=/usr/local/lib/ luajit ss_manager.lua

clean:
	@rm -fv *.so 