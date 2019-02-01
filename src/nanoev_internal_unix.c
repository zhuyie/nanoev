#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

int global_init()
{
    return NANOEV_SUCCESS;
}

void global_term()
{
}

/*----------------------------------------------------------------------------*/

int  mutex_init(mutex *m)
{
    return pthread_mutex_init(m, NULL);
}

void mutex_uninit(mutex *m)
{
    pthread_mutex_destroy(m);
}

void mutex_lock(mutex *m)
{
    pthread_mutex_lock(m);
}

void mutex_unlock(mutex *m)
{
    pthread_mutex_unlock(m);
}

/*----------------------------------------------------------------------------*/

thread_t get_current_thread()
{
    return pthread_self();
}

/*----------------------------------------------------------------------------*/

void time_now(nanoev_timeval *tv)
{
    gettimeofday(tv, NULL);
}

/*----------------------------------------------------------------------------*/

int set_non_blocking(SOCKET sock, int set)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) 
        return 0;
    flags = set ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return (fcntl(sock, F_SETFL, flags) == 0) ? 1 : 0;
}

void close_socket(SOCKET sock)
{
    close(sock);
}

int socket_last_error()
{
    return errno;
}
