CC=gcc
PREFIX=/usr/local
LUA_VERSION = 5.1
LUA_LIBDIR= $(PREFIX)/lib/lua/$(LUA_VERSION)
LUA_INC += -I$(PREFIX)/include -I$(PREFIX)/include/lua$(LUA_VERSION) -I$(PREFIX)/include/lua/$(LUA_VERSION) 
LUA_INC += -I/usr/include -I/usr/include/lua$(LUA_VERSION) -I/usr/include/lua/$(LUA_VERSION) 
OPT= -shared 
CFLAGS= -g -O0 -Wall -fPIC $(LUA_INC)


Z_CFLAGS=#-I/usr/local/include
Z_LDFLAGS=#-L/usr/local/lib -lz

UV_CFLAGS=-I/usr/local/include
UV_LDFLAGS=-L/usr/local/lib -luv 

LLHTTP_CFLAGS=-I/usr/local/include
LLHTTP_LDFLAGS=-L/usr/local/lib -lllhttp 







all: lite/uv.so

#-DLITE_CURL

lite/uv.so: lite/uv.c lite/ws_parser/ws_parser.c
	@echo CC $@
	@$(CC) $(OPT)  $(CFLAGS) -DLITE_MEM -DLITE_HTTP  $(LLHTTP_CFLAGS) $(UV_CFLAGS) -o $@ $^ $(UV_LDFLAGS) $(LLHTTP_LDFLAGS) 

lite/sha1.so: lite/sha1.c
	@echo CC $@
	@$(CC) $(OPT)  $(CFLAGS) -o $@ $^ 


test: lite/uv.so lite/sha1.so
	LD_LIBRARY_PATH=/usr/local/lib/ luajit -v 
	LD_LIBRARY_PATH=/usr/local/lib/ luajit test.lua 

clean:
	@rm -fv *.so 