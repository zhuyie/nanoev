#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

int global_init(void)
{
    return NANOEV_SUCCESS;
}

void global_term(void)
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

int cond_init(cond *c)
{
    return pthread_cond_init(c, NULL);
}

void cond_uninit(cond *c)
{
    pthread_cond_destroy(c);
}

void cond_wait(cond *c, mutex *m)
{
    pthread_cond_wait(c, m);
}

void cond_signal(cond *c)
{
    pthread_cond_signal(c);
}

void cond_broadcast(cond *c)
{
    pthread_cond_broadcast(c);
}

/*----------------------------------------------------------------------------*/

thread_t get_current_thread(void)
{
    return pthread_self();
}

struct thread_start {
    thread_callback callback;
    void *arg;
};

static void* thread_entry(void *arg)
{
    struct thread_start *start = (struct thread_start*)arg;
    thread_callback callback = start->callback;
    void *callback_arg = start->arg;

    mem_free(start);
    callback(callback_arg);

    return NULL;
}

int thread_create(thread_handle *thread, thread_callback callback, void *arg)
{
    struct thread_start *start;
    int ret;

    if (!thread || !callback)
        return NANOEV_ERROR_INVALID_ARG;

    start = (struct thread_start*)mem_alloc(sizeof(struct thread_start));
    if (!start)
        return NANOEV_ERROR_OUT_OF_MEMORY;

    start->callback = callback;
    start->arg = arg;

    ret = pthread_create(thread, NULL, thread_entry, start);
    if (ret) {
        mem_free(start);
        return NANOEV_ERROR_FAIL;
    }

    return NANOEV_SUCCESS;
}

void thread_join(thread_handle thread)
{
    pthread_join(thread, NULL);
}

void thread_detach(thread_handle thread)
{
    pthread_detach(thread);
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

int socket_last_error(void)
{
    return errno;
}

int socket_would_block(int error_code)
{
    return error_code == EAGAIN || error_code == EWOULDBLOCK;
}
