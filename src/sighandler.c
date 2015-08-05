/*
 * Astra Main App (OS signal handling)
 * http://cesbo.com/astra
 *
 * Copyright (C) 2015, Artem Kharitonov <artem@sysert.ru>
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

#include <astra.h>
#include "sighandler.h"

#ifndef _WIN32
#include <signal.h>
#include <pthread.h>

/* TODO: move these to core/thread.h */
#define mutex_lock(__mutex) pthread_mutex_lock(&__mutex)
#define mutex_timedlock(__mutex, __ms) __mutex_timedlock(&__mutex, (__ms))
#define mutex_unlock(__mutex) pthread_mutex_unlock(&__mutex)

struct signal_setup
{
    const int signum;
    const bool ignore;
    sighandler_t oldhandler;
};

static struct signal_setup siglist[] = {
    { SIGHUP,  false, NULL },
    { SIGINT,  false, NULL },
    { SIGQUIT, false, NULL },
    { SIGUSR1, false, NULL },
    { SIGTERM, false, NULL },
    { SIGPIPE, true, NULL },
};

static sigset_t block_mask;
static sigset_t old_mask;

static pthread_t signal_thread;
static pthread_mutex_t signal_lock = PTHREAD_MUTEX_INITIALIZER;
static bool quit_thread = true;

static bool __mutex_timedlock(pthread_mutex_t *mutex, unsigned ms)
{
    struct timespec ts = { 0, 0 };
#ifdef HAVE_CLOCK_GETTIME
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
#endif
        ts.tv_sec = time(NULL);

    /* try not to overflow tv_nsec */
    ts.tv_sec += (ms / 1000);
    ts.tv_nsec += (ms % 1000) * 1000000;
    ts.tv_sec += ts.tv_nsec / 1000000000;
    ts.tv_nsec %= 1000000000;

    const int ret = pthread_mutex_timedlock(mutex, &ts);
    return (ret == 0);
}

static void perror_exit(int errnum, const char *str)
{
    fprintf(stderr, "%s: %s\n", str, strerror(errnum));
    _exit(EXIT_FAILURE);
}

static void *thread_loop(void *arg)
{
    __uarg(arg);

    while (true)
    {
        int signum;
        const int ret = sigwait(&block_mask, &signum);
        if (ret != 0)
            perror_exit(ret, "sigwait()");

        pthread_mutex_lock(&signal_lock);
        if (quit_thread)
        {
            /* signal handling is being shut down */
            pthread_mutex_unlock(&signal_lock);
            pthread_exit(NULL);
        }

        switch (signum)
        {
            case SIGINT:
            case SIGTERM:
                astra_shutdown();
                break;

            case SIGUSR1:
                astra_reload();
                break;

            case SIGHUP:
                astra_sighup();
                break;

            case SIGQUIT:
                astra_abort();
                break;

            default:
                break;
        }

        pthread_mutex_unlock(&signal_lock);
    }

    return NULL;
}

static void signal_cleanup(void)
{
    /* ask signal thread to quit */
    quit_thread = true;
    pthread_mutex_unlock(&signal_lock);
    if (pthread_kill(signal_thread, SIGTERM) == 0)
        pthread_join(signal_thread, NULL);

    /* restore old handlers for ignored signals */
    for (size_t i = 0; i < ASC_ARRAY_SIZE(siglist); i++)
    {
        const struct signal_setup *const ss = &siglist[i];
        if (ss->ignore)
            signal(ss->signum, ss->oldhandler);
    }

    /* restore old signal mask */
    const int ret = pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
    if (ret != 0)
        perror_exit(ret, "pthread_sigmask()");
}

void signal_setup(void)
{
    /* block signals of interest before starting any threads */
    sigemptyset(&block_mask);

    for (size_t i = 0; i < ASC_ARRAY_SIZE(siglist); i++)
    {
        struct signal_setup *const ss = &siglist[i];
        if (ss->ignore)
        {
            /* doesn't matter which thread gets to ignore it */
            ss->oldhandler = signal(ss->signum, SIG_IGN);
            if (ss->oldhandler == SIG_ERR)
                perror_exit(errno, "signal()");
        }
        else
        {
            sigaddset(&block_mask, ss->signum);
        }
    }

    int ret = pthread_sigmask(SIG_BLOCK, &block_mask, &old_mask);
    if (ret != 0)
        perror_exit(ret, "pthread_sigmask()");

    /* delay any actions until main thread finishes initialization */
    ret = pthread_mutex_lock(&signal_lock);
    if (ret != 0)
        perror_exit(ret, "pthread_mutex_lock()");

    /* start our signal handling thread */
    quit_thread = false;
    ret = pthread_create(&signal_thread, NULL, &thread_loop, NULL);
    if (ret != 0)
        perror_exit(ret, "pthread_create()");

    atexit(signal_cleanup);
}

#else /* !_WIN32 */

/* TODO: move these to core/thread.h */
#define mutex_lock(__mutex) WaitForSingleObject(__mutex, INFINITE)
#define mutex_timedlock(__mutex, __ms) __mutex_timedlock(__mutex, (__ms))
#define mutex_unlock(__mutex) while (ReleaseMutex(__mutex))

#define SERVICE_NAME (char *)PACKAGE_NAME

static SERVICE_STATUS service_status;
static SERVICE_STATUS_HANDLE service_status_handle = NULL;
static HANDLE service_event = NULL;
static HANDLE service_thread = NULL;

static HANDLE signal_lock = NULL;
static bool ignore_ctrl = true;

static bool __mutex_timedlock(HANDLE mutex, unsigned ms)
{
    DWORD ret = WaitForSingleObject(mutex, ms);
    return (ret == WAIT_OBJECT_0);
}

static void perror_exit(DWORD errnum, const char *str)
{
    LPTSTR msg = NULL;
    FormatMessage((FORMAT_MESSAGE_FROM_SYSTEM
                   | FORMAT_MESSAGE_ALLOCATE_BUFFER
                   | FORMAT_MESSAGE_IGNORE_INSERTS)
                  , NULL, errnum, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
                  , (LPTSTR)&msg, 0, NULL);

    /* NOTE: FormatMessage() appends a newline to error message */
    fprintf(stderr, "%s: %s", str, msg);
    _exit(EXIT_FAILURE);
}

#ifdef DEBUG
static int reopen(int fd, const char *pathname)
{
    const int file = open(pathname, O_WRONLY | O_CREAT | O_APPEND
                          , S_IRUSR | S_IWUSR);

    if (file == -1)
        return -1;

    const int ret = dup2(file, fd);
    close(file);

    return ret;
}

static void redirect_stdio(void)
{
    static const char logfile[] = "\\stdio.log";

    char buf[MAX_PATH];
    if (!GetModuleFileName(NULL, buf, sizeof(buf)))
        perror_exit(GetLastError(), "GetModuleFileName()");

    char *const p = strrchr(buf, '\\');
    if (p != NULL)
        *p = '\0';
    else
        strncpy(buf, ".", sizeof("."));

    strncat(buf, logfile, sizeof(buf) - strlen(buf) - 1);

    if (reopen(STDOUT_FILENO, buf) == -1)
        perror_exit(errno, "reopen()");

    if (reopen(STDERR_FILENO, buf) == -1)
        perror_exit(errno, "reopen()");

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}
#endif

static inline void service_set_state(DWORD state)
{
    service_status.dwCurrentState = state;
    SetServiceStatus(service_status_handle, &service_status);
}

static void WINAPI service_handler(DWORD control)
{
    switch (control)
    {
        case SERVICE_CONTROL_STOP:
            if (service_status.dwCurrentState == SERVICE_RUNNING)
            {
                /*
                 * NOTE: stop pending state should really be set by
                 *       signal_enable(), but then we'd have to somehow
                 *       filter out soft restart requests.
                 */
                service_set_state(SERVICE_STOP_PENDING);

                mutex_lock(signal_lock);
                if (!ignore_ctrl)
                    astra_shutdown();
                mutex_unlock(signal_lock);
            }
            break;

        case SERVICE_CONTROL_INTERROGATE:
            service_set_state(service_status.dwCurrentState);
            break;

        default:
            break;
    }
}

static BOOL WINAPI console_handler(DWORD type)
{
    /* handlers are run in separate threads */
    mutex_lock(signal_lock);
    if (ignore_ctrl)
    {
        mutex_unlock(signal_lock);
        return true;
    }

    bool ret = true;
    switch (type)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            astra_shutdown();
            break;

        default:
            ret = false;
    }

    mutex_unlock(signal_lock);
    return ret;
}

static void WINAPI service_main(DWORD argc, LPTSTR *argv)
{
    __uarg(argc);
    __uarg(argv);

#ifdef DEBUG
    redirect_stdio();
#endif /* DEBUG */

    /* register control handler */
    ignore_ctrl = false;
    service_status_handle =
        RegisterServiceCtrlHandler(SERVICE_NAME, service_handler);

    if (service_status_handle == NULL)
        perror_exit(GetLastError(), "RegisterServiceCtrlHandler()");

    /* report to SCM */
    service_set_state(SERVICE_START_PENDING);

    /* notify main thread */
    SetEvent(service_event);
}

static DWORD WINAPI service_thread_proc(void *arg)
{
    __uarg(arg);

    /*
     * NOTE: here we use a dedicated thread for the blocking call
     *       to StartServiceCtrlDispatcher(), which allows us to keep
     *       the normal startup routine unaltered.
     */
    static const SERVICE_TABLE_ENTRY svclist[] = {
        { SERVICE_NAME, service_main },
        { NULL, NULL },
    };

    if (!StartServiceCtrlDispatcher(svclist))
        return GetLastError();

    return ERROR_SUCCESS;
}

static bool service_initialize(void)
{
    /* initialize service state struct */
    memset(&service_status, 0, sizeof(service_status));
    service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    service_status.dwCurrentState = SERVICE_STOPPED;

    /* attempt to connect to SCM */
    service_event = CreateEvent(NULL, false, false, NULL);
    if (service_event == NULL)
        perror_exit(GetLastError(), "CreateEvent()");

    service_thread = CreateThread(NULL, 0, service_thread_proc, NULL, 0, NULL);
    if (service_thread == NULL)
        perror_exit(GetLastError(), "CreateThread()");

    const HANDLE handles[2] = { service_event, service_thread };
    const DWORD ret = WaitForMultipleObjects(2, handles, false, INFINITE);
    ASC_FREE(service_event, CloseHandle);

    if (ret == WAIT_OBJECT_0)
    {
        /* service_event fired; SCM connection successful */
        return true;
    }
    else if (ret == WAIT_OBJECT_0 + 1)
    {
        /* service_thread exited; we're probably running from a console */
        DWORD exit_code = ERROR_INTERNAL_ERROR;
        if (GetExitCodeThread(service_thread, &exit_code))
        {
            if (exit_code == ERROR_SUCCESS)
                /* shouldn't return success this early */
                exit_code = ERROR_INTERNAL_ERROR;
        }

        if (exit_code != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
            perror_exit(exit_code, "StartServiceCtrlDispatcher()");

        ASC_FREE(service_thread, CloseHandle);
    }
    else
    {
        /* shouldn't happen */
        if (ret != WAIT_FAILED)
            SetLastError(ERROR_INTERNAL_ERROR);

        perror_exit(GetLastError(), "WaitForMultipleObjects()");
    }

    return false;
}

static bool service_destroy(void)
{
    if (service_thread != NULL)
    {
        /* notify SCM if we're exiting because of an error */
        if (astra_exit_status != EXIT_SUCCESS)
        {
            service_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            service_status.dwServiceSpecificExitCode = astra_exit_status;
        }

        /* report service shutdown, join dispatcher thread */
        if (service_status_handle != NULL)
            service_set_state(SERVICE_STOPPED);
        else
            TerminateThread(service_thread, ERROR_SUCCESS);

        WaitForSingleObject(service_thread, INFINITE);
        ASC_FREE(service_thread, CloseHandle);

        /* reset service state */
        memset(&service_status, 0, sizeof(service_status));
        service_status_handle = NULL;

        return true;
    }

    return false;
}

static void signal_cleanup(void)
{
    /* dismiss any threads waiting for lock */
    ignore_ctrl = true;
    mutex_unlock(signal_lock);

    if (!service_destroy())
    {
        /* remove ctrl handler; this also joins handler threads */
        if (!SetConsoleCtrlHandler(console_handler, false))
            perror_exit(GetLastError(), "SetConsoleCtrlHandler()");
    }

    /* free mutex */
    ASC_FREE(signal_lock, CloseHandle);
}

void signal_setup(void)
{
    /* create and lock signal handling mutex */
    signal_lock = CreateMutex(NULL, true, NULL);
    if (signal_lock == NULL)
        perror_exit(GetLastError(), "CreateMutex()");

    /* install console control handler if not started as a service */
    if (!service_initialize())
    {
        ignore_ctrl = false;
        if (!SetConsoleCtrlHandler(console_handler, true))
            perror_exit(GetLastError(), "SetConsoleCtrlHandler()");
    }

    atexit(signal_cleanup);
}
#endif /* !_WIN32 */

void signal_enable(bool running)
{
    if (running)
    {
#ifdef _WIN32
        /* mark service as running on first init */
        if (service_status.dwCurrentState == SERVICE_START_PENDING)
            service_set_state(SERVICE_RUNNING);
#endif /* !_WIN32 */

        mutex_unlock(signal_lock);
    }
    else
        mutex_lock(signal_lock);
}
