/*
 * Astra Module: File Output
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2015, Andrey Dyldin <and@cesbo.com>
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
 *      file_output
 *
 * Module Options:
 *      filename    - string, output file name
 *      m2ts        - boolean, use m2ts file format [default : false]
 *      buffer_size - number, output buffer size. in kilobytes [default : 32]
 *      aio         - boolean, use aio [default : false]
 *      directio    - boolean, try to avoid all caching operations [default : false]
 *
 * Module Methods:
 *      status      - return table with items:
 *                    size      - number, current file size
 */

#include <astra.h>
#include <luaapi/stream.h>

#ifdef HAVE_AIO
#   include <aio.h>
#   ifdef HAVE_LIBAIO
#       include <libaio.h>
#   endif
#endif

#define FILE_BUFFER_SIZE 32

#define ALIGN 4096
#define align(_size) ((_size / ALIGN) * ALIGN)

#define MSG(_msg) "[file_output %s] " _msg, mod->config.filename

struct module_data_t
{
    MODULE_STREAM_DATA();

    struct
    {
        const char *filename;
#ifdef O_DIRECT
        bool directio;
#endif

#ifdef HAVE_AIO
        bool aio;
#endif

#ifdef HAVE_LIBAIO
        bool aio_kernel;
#endif
    } config;

    int fd;
    bool error;

#ifdef HAVE_AIO
    struct aiocb aiocb;
    void *buffer_aio;
#endif

#ifdef HAVE_LIBAIO
    io_context_t ctx;
    struct iocb *io[1];
#endif

    size_t file_size;

    uint8_t packet_size;
    size_t buffer_size;
    size_t buffer_skip;
    uint8_t *buffer; // write buffer
};

/* stream_ts callbacks */

static void module_destroy(module_data_t *mod);

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    if(mod->buffer_skip + mod->packet_size > mod->buffer_size || !ts)
    {
        ssize_t size;
#ifdef HAVE_AIO
        if(mod->config.aio)
        {
            size = align(mod->buffer_skip);

#ifdef HAVE_LIBAIO
            if(mod->config.aio_kernel)
            {
                if(!mod->io[0])
                    mod->io[0] = ASC_ALLOC(1, struct iocb);
                else
                {
                    struct io_event events[1];
                    if(!io_getevents(mod->ctx, 1, 1, events, 0))
                    {
                        if(!mod->error)
                        {
                            asc_log_error(MSG("io_submit in progress. "
                                              "Try to increase buffer size"));
                            mod->error = true;
                        }
                        return;
                    }
                }

                memcpy(mod->buffer_aio, mod->buffer, size);
                io_prep_pwrite(mod->io[0], mod->fd, mod->buffer_aio, size, mod->file_size);
                if(io_submit(mod->ctx, 1, mod->io) != 1)
                {
                    asc_log_error(MSG("Error at io_submit"));
                    mod->error = true;
                    module_destroy(mod);
                    return;
                }
            }
            else
#endif /* HAVE_LIBAIO */
            {
                int error = aio_error(&mod->aiocb);
                if(error == EINPROGRESS)
                {
                    if(!mod->error)
                    {
                        asc_log_error(MSG("aio_write in progress. "
                                          "Try to increase buffer size"));
                        mod->error = true;
                    }
                    return;
                }
                else if(error != 0)
                {
                    asc_log_error(MSG("Error at aio_write: %s"), strerror(errno));
                    mod->error = true;
                    module_destroy(mod);
                    return;
                }

                memcpy(mod->buffer_aio, mod->buffer, size);
                mod->aiocb.aio_nbytes = size;

                aio_write(&mod->aiocb);
            }
        }
        else
#endif /* HAVE_AIO */
        { /* !mod->aio */
#ifdef O_DIRECT
            size = mod->config.directio ? align(mod->buffer_skip) : mod->buffer_skip;
#else
            size = mod->buffer_skip;
#endif

            if(write(mod->fd, mod->buffer, size) != size)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if(!mod->error)
                    {
                        asc_log_error(MSG("skip packets at write due to error"));
                        mod->error = true;
                    }
                }
                else
                {
                    asc_log_error(MSG("write error: %s"), strerror(errno));
                    mod->error = true;
                    module_destroy(mod);
                }
                return;
            }
        }

        mod->buffer_skip -= size;
        if(size && mod->buffer_skip)
            memmove(mod->buffer, &mod->buffer[size], mod->buffer_skip);
        mod->file_size += size;

        if(!ts)
            return;
    }

    if(mod->packet_size == TS_PACKET_SIZE)
    {
        memcpy(&mod->buffer[mod->buffer_skip], ts, TS_PACKET_SIZE);
        mod->buffer_skip += TS_PACKET_SIZE;
    }
    else
    {
        const uint64_t t = asc_utime() / 1000;
        mod->buffer[0 + mod->buffer_skip] = (t >> 24) & 0xFF;
        mod->buffer[1 + mod->buffer_skip] = (t >> 16) & 0xFF;
        mod->buffer[2 + mod->buffer_skip] = (t >>  8) & 0xFF;
        mod->buffer[3 + mod->buffer_skip] = (t      ) & 0xFF;
        memcpy(&mod->buffer[4 + mod->buffer_skip], ts, TS_PACKET_SIZE);
        mod->buffer_skip += M2TS_PACKET_SIZE;
    }
}

/* methods */

static int method_status(lua_State *L, module_data_t *mod)
{
    lua_newtable(L);

    lua_pushnumber(L, mod->file_size);
    lua_setfield(L, -2, "size");

    return 1;
}

/* required */

static void module_init(lua_State *L, module_data_t *mod)
{
    module_option_string(L, "filename", &mod->config.filename, NULL);
    if(!mod->config.filename)
    {
        asc_log_error("[file_output] option 'filename' is required");
        asc_lib_abort();
    }

    bool m2ts = 0;
    module_option_boolean(L, "m2ts", &m2ts);
    mod->packet_size = (m2ts) ? M2TS_PACKET_SIZE : TS_PACKET_SIZE;

#ifdef O_DIRECT
    module_option_boolean(L, "directio", &mod->config.directio);
#endif

#ifdef HAVE_AIO
    module_option_boolean(L, "aio", &mod->config.aio);
#endif

#ifdef HAVE_LIBAIO
    mod->config.aio_kernel = mod->config.aio && mod->config.directio;
#endif

    int buffer_size = FILE_BUFFER_SIZE;
    module_option_integer(L, "buffer_size", &buffer_size);
    mod->buffer_size = buffer_size * 1024;

#if defined(HAVE_POSIX_MEMALIGN) && defined(O_DIRECT)
#ifdef HAVE_AIO
    if(mod->config.directio && !mod->config.aio)
#else
    if(mod->config.directio)
#endif
    {
        if(posix_memalign((void **)&mod->buffer, ALIGN, mod->buffer_size))
        {
            asc_log_error(MSG("cannot malloc aligned memory"));
            asc_lib_abort();
        }
    }
    else
#endif
    {
        mod->buffer = ASC_ALLOC(mod->buffer_size, uint8_t);
    }

    int flags = O_CREAT | O_APPEND | O_WRONLY;
    const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

#ifdef HAVE_AIO
    flags |= O_NONBLOCK;
#endif

#ifdef O_DIRECT
    if(mod->config.directio)
        flags |= O_DIRECT;
#endif

    mod->fd = open(mod->config.filename, flags, mode);
    if(mod->fd <= 0)
    {
        asc_log_error(MSG("failed to open file [%s]"), strerror(errno));
        asc_lib_abort();
    }

    struct stat st;
    fstat(mod->fd, &st);
    mod->file_size = st.st_size;

#ifdef HAVE_AIO
    if(mod->config.aio)
    {
#ifdef HAVE_LIBAIO
        if(mod->config.aio_kernel)
        {
#ifdef HAVE_POSIX_MEMALIGN
            if(posix_memalign(&mod->buffer_aio, ALIGN, mod->buffer_size))
            {
                asc_log_error(MSG("cannot malloc aligned memory"));
                asc_lib_abort();
            }
#else /* !HAVE_POSIX_MEMALIGN */
            mod->buffer_aio = ASC_ALLOC(mod->buffer_size, uint8_t);
#endif /* HAVE_POSIX_MEMALIGN */
            memset(&mod->ctx, 0, sizeof(mod->ctx));
            io_queue_init(1, &mod->ctx);
            mod->io[0] = NULL;
        }
        else
#endif /* HAVE_LIBAIO */
        { /* !mod->aio_kernel */
            mod->buffer_aio = ASC_ALLOC(mod->buffer_size, uint8_t);

            memset(&mod->aiocb, 0, sizeof(struct aiocb));
            mod->aiocb.aio_fildes = mod->fd;
            mod->aiocb.aio_buf = mod->buffer_aio;
            mod->aiocb.aio_lio_opcode = LIO_WRITE;
            mod->aiocb.aio_sigevent.sigev_notify = SIGEV_NONE;
        }
    } /* mod->aio */
#endif /* HAVE_AIO */

    module_stream_init(mod, on_ts);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

#ifdef HAVE_AIO
    if(mod->config.aio)
    {
#ifdef HAVE_LIBAIO
        if(mod->config.aio_kernel)
        {
            struct io_event events[1];
            io_cancel(mod->ctx, mod->io[0], events);
            io_destroy(mod->ctx);
            ASC_FREE(mod->io[0], free);
        }
        else
#endif
        {
            const int error = aio_error(&mod->aiocb);
            if(error == EINPROGRESS)
                aio_cancel(mod->fd, &mod->aiocb);
        }
    }
    else if(!mod->error)
        on_ts(mod, NULL); /* Flush buffer */
#endif

    if(mod->fd > 0)
    {
        close(mod->fd);
        mod->fd = 0;
    }

    ASC_FREE(mod->buffer, free);

#ifdef HAVE_AIO
    ASC_FREE(mod->buffer_aio, free);
#endif
}

MODULE_LUA_METHODS()
{
    { "status", method_status },
};
MODULE_LUA_REGISTER(file_output)
