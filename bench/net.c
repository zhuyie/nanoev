#include "net.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
# include <netdb.h>
#endif

int bench_resolve_addr(const bench_config *config, bench_sockaddr *addr)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    char service[6];
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = config->family == bench_family_ipv6 ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = config->role == bench_role_server ? AI_PASSIVE : 0;
    snprintf(service, sizeof(service), "%u", (unsigned int)config->port);

    ret = getaddrinfo(config->host, service, &hints, &result);
    if (ret != 0 || !result)
        return -1;
    if (result->ai_addrlen > sizeof(addr->storage)) {
        freeaddrinfo(result);
        return -1;
    }
    memset(addr, 0, sizeof(*addr));
    memcpy(&addr->storage, result->ai_addr, result->ai_addrlen);
    addr->len = (int)result->ai_addrlen;
    freeaddrinfo(result);
    return 0;
}
