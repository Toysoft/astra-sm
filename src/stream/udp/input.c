/*
 * Astra Module: UDP Input
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2014, Andrey Dyldin <and@cesbo.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Module Name:
 *      udp_input
 *
 * Module Options:
 *      addr        - string, source IP address
 *      port        - number, source UDP port
 *      localaddr   - string, IP address of the local interface
 *      socket_size - number, socket buffer size
 *      renew       - number, renewing multicast subscription interval in seconds
 *      rtp         - boolean, use RTP instead of RAW UDP
 *
 * Module Methods:
 *      port()      - return number, random port number
 */

#include <astra.h>
#include <core/socket.h>
#include <core/timer.h>
#include <luaapi/stream.h>

#define UDP_BUFFER_SIZE 1460
#define RTP_HEADER_SIZE 12

#define RTP_IS_EXT(_data) ((_data[0] & 0x10))
#define RTP_EXT_SIZE(_data) \
    (((_data[RTP_HEADER_SIZE + 2] << 8) | _data[RTP_HEADER_SIZE + 3]) * 4 + 4)

#define MSG(_msg) "[udp_input %s:%d] " _msg, mod->config.addr, mod->config.port

struct module_data_t
{
    MODULE_STREAM_DATA();

    struct
    {
        const char *addr;
        int port;
        const char *localaddr;
        bool rtp;
    } config;

    bool is_error_message;

    asc_socket_t *sock;
    asc_timer_t *timer_renew;

    uint8_t buffer[UDP_BUFFER_SIZE];
};

static void on_close(void *arg)
{
    module_data_t *const mod = (module_data_t *)arg;

    if(mod->sock)
    {
        asc_socket_multicast_leave(mod->sock);
        asc_socket_close(mod->sock);
        mod->sock = NULL;
    }

    if(mod->timer_renew)
    {
        asc_timer_destroy(mod->timer_renew);
        mod->timer_renew = NULL;
    }
}

static void on_read(void *arg)
{
    module_data_t *const mod = (module_data_t *)arg;

    /* TODO: read until it fails with EAGAIN */
    const ssize_t ret = asc_socket_recv(mod->sock, mod->buffer, UDP_BUFFER_SIZE);
    if(ret <= 0)
    {
        if(ret == 0 || asc_socket_would_block())
            return;

        asc_log_error(MSG("recv(): %s"), asc_error_msg());
        on_close(mod);

        return;
    }

    const size_t len = ret;
    size_t i = 0;

    if(mod->config.rtp)
    {
        i = RTP_HEADER_SIZE;
        if(RTP_IS_EXT(mod->buffer))
        {
            if(len < RTP_HEADER_SIZE + 4)
                return;

            i += RTP_EXT_SIZE(mod->buffer);
        }
    }

    for(; i + TS_PACKET_SIZE <= len; i += TS_PACKET_SIZE)
        module_stream_send(mod, &mod->buffer[i]);

    if(i != len && !mod->is_error_message)
    {
        asc_log_error(MSG("wrong stream format. drop %zu bytes"), len - i);
        mod->is_error_message = true;
    }
}

static void timer_renew_callback(void *arg)
{
    module_data_t *const mod = (module_data_t *)arg;
    asc_socket_multicast_renew(mod->sock);
}

static int method_port(lua_State *L, module_data_t *mod)
{
    const int port = asc_socket_port(mod->sock);
    lua_pushinteger(L, port);

    return 1;
}

static void module_init(lua_State *L, module_data_t *mod)
{
    module_stream_init(mod, NULL);

    module_option_string(L, "addr", &mod->config.addr, NULL);
    if(mod->config.addr == NULL)
        luaL_error(L, "[udp_input] option 'addr' is required");

    module_option_integer(L, "port", &mod->config.port);

    mod->sock = asc_socket_open_udp4(mod);
    asc_socket_set_reuseaddr(mod->sock, 1);
#if defined(_WIN32) || defined(__CYGWIN__)
    if(!asc_socket_bind(mod->sock, NULL, mod->config.port))
#else
    if(!asc_socket_bind(mod->sock, mod->config.addr, mod->config.port))
#endif
        return;

    int value;
    if(module_option_integer(L, "socket_size", &value))
        asc_socket_set_buffer(mod->sock, value, 0);

    module_option_boolean(L, "rtp", &mod->config.rtp);

    asc_socket_set_on_read(mod->sock, on_read);
    asc_socket_set_on_close(mod->sock, on_close);

    module_option_string(L, "localaddr", &mod->config.localaddr, NULL);
    asc_socket_multicast_join(mod->sock, mod->config.addr, mod->config.localaddr);

    if(module_option_integer(L, "renew", &value))
        mod->timer_renew = asc_timer_init(value * 1000, timer_renew_callback, mod);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);
    on_close(mod);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF(),
    { "port", method_port },
};
MODULE_LUA_REGISTER(udp_input)
