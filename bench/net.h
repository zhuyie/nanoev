#ifndef NANOEV_BENCH_NET_H
#define NANOEV_BENCH_NET_H

#include "tcp.h"

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <sys/socket.h>
#endif

typedef struct bench_sockaddr {
    struct sockaddr_storage storage;
    int len;
} bench_sockaddr;

int bench_resolve_addr(const bench_config *config, bench_sockaddr *addr);

#endif
