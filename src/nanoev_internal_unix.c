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

void time_now(nanoev_timeval *tv)
{
    gettimeofday(tv, NULL);
}

/*----------------------------------------------------------------------------*/

int set_non_blocking(SOCKET sock, int blocking)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) 
        return 0;
    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
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
