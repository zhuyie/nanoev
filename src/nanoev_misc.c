#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

void* mem_alloc(size_t sz)
{
    return malloc(sz);
}

void* mem_realloc(void *mem, size_t sz)
{
    return realloc(mem, sz);
}

void  mem_free(void *mem)
{
    free(mem);
}

/*----------------------------------------------------------------------------*/

void nanoev_now(nanoev_timeval *tv)
{
    time_now(tv);
}

void time_add(nanoev_timeval *tv, const nanoev_timeval *add)
{
    tv->tv_sec += add->tv_sec;
    tv->tv_usec += add->tv_usec;
    ASSERT(tv->tv_usec < 2000000);
    if (tv->tv_usec >= 1000000) {
        tv->tv_sec += 1;
        tv->tv_usec -= 1000000;
    }
}

void time_sub(nanoev_timeval *tv, const nanoev_timeval *sub)
{
    ASSERT(tv->tv_sec >= sub->tv_sec);
    tv->tv_sec -= sub->tv_sec;
    if (sub->tv_usec > tv->tv_usec) {
        ASSERT(tv->tv_sec > 0);
        tv->tv_sec -= 1;
        tv->tv_usec = tv->tv_usec + 1000000 - sub->tv_usec;
        ASSERT(tv->tv_usec < 1000000);
    } else {
        tv->tv_usec -= sub->tv_usec;
    }
}

int time_cmp(const nanoev_timeval *tv0, const nanoev_timeval *tv1)
{
    if (tv0->tv_sec > tv1->tv_sec) {
        return 1;
    } else if (tv0->tv_sec < tv1->tv_sec) {
        return -1;
    } else {
        if (tv0->tv_usec > tv1->tv_usec)
            return 1;
        else if (tv0->tv_usec < tv1->tv_usec)
            return -1;
        else
            return 0;
    }
}

/*----------------------------------------------------------------------------*/

int nanoev_addr_init(
    struct nanoev_addr *addr, 
    int family,
    const char *ip, 
    unsigned short port
    )
{
    ASSERT(addr && ip && port);

    memset(addr, 0, sizeof(struct nanoev_addr));
    
    if (family == NANOEV_AF_INET) {
        struct sockaddr_in *_addr = (struct sockaddr_in*)addr;
        _addr->sin_family = AF_INET;
        if (inet_pton(AF_INET, ip, &_addr->sin_addr) != 1) {
            return NANOEV_ERROR_INVALID_ARG;
        }
        _addr->sin_addr.s_addr = inet_addr(ip);
        _addr->sin_port = htons(port);
        return NANOEV_SUCCESS;

    } else if (family == NANOEV_AF_INET6) {
        struct sockaddr_in6 *_addr = (struct sockaddr_in6*)addr;
        _addr->sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, ip, &_addr->sin6_addr) != 1) {
            return NANOEV_ERROR_INVALID_ARG;
        }
        _addr->sin6_port = htons(port);
        return NANOEV_SUCCESS;

    } else {
        return NANOEV_ERROR_INVALID_ARG;
    }
}

int nanoev_addr_get_ip(
    const struct nanoev_addr *addr, 
    char *ip,
    int ip_len
    )
{
    ASSERT(addr && ip);

    if (addr->ss_family == AF_INET) {
        struct sockaddr_in *_addr = (struct sockaddr_in*)addr;
        /* xxx.xxx.xxx.xxx */
        if (ip_len < 16) {
            return NANOEV_ERROR_INVALID_ARG;
        }
        if (!inet_ntop(AF_INET, &_addr->sin_addr, ip, ip_len)) {
            return NANOEV_ERROR_INVALID_ARG;
        }
        return NANOEV_SUCCESS;

    } else if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6 *_addr = (struct sockaddr_in6*)addr;
        /* xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx */
        /* xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:yyy.yyy.yyy.yyy */
        if (ip_len < 46) {
            return NANOEV_ERROR_INVALID_ARG;
        }
        if (!inet_ntop(AF_INET6, &_addr->sin6_addr, ip, ip_len)) {
            return NANOEV_ERROR_INVALID_ARG;
        }
        return NANOEV_SUCCESS;
    
    } else {
        return NANOEV_ERROR_INVALID_ARG;
    }
}

int nanoev_addr_get_port(
    const struct nanoev_addr *addr, 
    unsigned short *port
    )
{
    ASSERT(addr && port);

    if (addr->ss_family == AF_INET) {
        struct sockaddr_in *_addr = (struct sockaddr_in*)addr;
        *port = ntohs(_addr->sin_port);
        return NANOEV_SUCCESS;

    } else if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6 *_addr = (struct sockaddr_in6*)addr;
        *port = ntohs(_addr->sin6_port);
        return NANOEV_SUCCESS;

    } else {
        return NANOEV_ERROR_INVALID_ARG;
    }
}
