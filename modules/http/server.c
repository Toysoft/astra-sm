/*
 * For more information, visit https://cesbo.com
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 */

#include <astra.h>

#define LOG_MSG(_msg) "[http_server %s:%d] " _msg \
        , mod->config.addr, mod->config.port

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        const char *addr;
        int port;
    } config;

    int idx_self;

    int sock;
};

static const char __callback[] = "callback";
static const char __default_addr[] = "0.0.0.0";

/* callbacks */

static int method_close(module_data_t *);

static void accept_callback(void *arg, int event)
{
    module_data_t *mod = arg;

    if(event == EVENT_ERROR)
    {
        method_close(mod);
        return;
    }

    char addr[16];
    int port;
    int csock = socket_accept(mod->sock, addr, &port);
    if(!csock)
        return;

    lua_State *L = LUA_STATE(mod);

    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->__idx_options);
    const int options = lua_gettop(L);

    lua_getglobal(L, "http_client");
    lua_newtable(L);
    lua_getfield(L, options, __callback);
    lua_setfield(L, -2, __callback);
    lua_pushnumber(L, csock);
    lua_setfield(L, -2, "fd");
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->idx_self);
    lua_setfield(L, -2, "server");
    lua_pushstring(L, addr);
    lua_setfield(L, -2, "addr");
    lua_pushnumber(L, port);
    lua_setfield(L, -2, "port");
    lua_call(L, 1, 0);

    lua_pop(L, 1); // options
}

/* methods */

static int method_close(module_data_t *mod)
{
    if(mod->sock > 0)
    {
        event_detach(mod->sock);
        socket_close(mod->sock);
        mod->sock = 0;
    }

    if(mod->idx_self > 0)
    {
        lua_State *L = LUA_STATE(mod);
        luaL_unref(L, LUA_REGISTRYINDEX, mod->idx_self);
        mod->idx_self = 0;
    }

    return 0;
}

static int method_port(module_data_t *mod)
{
    int port = socket_port(mod->sock);
    lua_State *L = LUA_STATE(mod);
    lua_pushnumber(L, port);
    return 1;
}

/* required */

static void module_init(module_data_t *mod)
{
    if(!mod->config.addr)
        mod->config.addr = __default_addr;

#ifdef LOG_DEBUG
    log_debug(LOG_MSG("init"));
#endif

    lua_State *L = LUA_STATE(mod);

    // options table
    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->__idx_options);
    lua_getfield(L, -1, __callback);
    if(lua_isnil(L, -1))
        luaL_error(L, LOG_MSG("callback required"));
    lua_pop(L, 2); // callback + options

    // store self in registry
    lua_pushvalue(L, -1);
    mod->idx_self = luaL_ref(L, LUA_REGISTRYINDEX);

    mod->sock = socket_open(SOCKET_BIND | SOCKET_REUSEADDR
                            , mod->config.addr, mod->config.port);
    if(!mod->sock)
        method_close(mod);
    event_attach(mod->sock, accept_callback, mod, EVENT_READ);
    log_debug(LOG_MSG("fd=%d"), mod->sock);
}

static void module_destroy(module_data_t *mod)
{
#ifdef LOG_DEBUG
    log_debug(LOG_MSG("destroy"));
#endif

    method_close(mod);
}

MODULE_OPTIONS()
{
    OPTION_STRING("addr", config.addr, NULL)
    OPTION_NUMBER("port", config.port, NULL)
    OPTION_CUSTOM("callback", NULL)
};

MODULE_METHODS()
{
    METHOD(close)
    METHOD(port)
};

MODULE(http_server)
