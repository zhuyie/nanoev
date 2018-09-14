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

void nanoev_now(struct nanoev_timeval *tv)
{
    time_now(tv);
}

void time_add(struct nanoev_timeval *tv, const struct nanoev_timeval *add)
{
    tv->tv_sec += add->tv_sec;
    tv->tv_usec += add->tv_usec;
    ASSERT(tv->tv_usec < 2000000);
    if (tv->tv_usec >= 1000000) {
        tv->tv_sec += 1;
        tv->tv_usec -= 1000000;
    }
}

void time_sub(struct nanoev_timeval *tv, const struct nanoev_timeval *sub)
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

int time_cmp(const struct nanoev_timeval *tv0, const struct nanoev_timeval *tv1)
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

void nanoev_addr_init(
    struct nanoev_addr *addr, 
    const char *ip, 
    unsigned short port
    )
{
    ASSERT(addr && ip && port);
    addr->ip = inet_addr(ip);
    addr->port = htons(port);
}

void nanoev_addr_get_ip(
    const struct nanoev_addr *addr, 
    char ip[16]
    )
{
    const char *s;

    ASSERT(addr && ip);

    s = inet_ntoa(*(struct in_addr*)&addr->ip);
    if (s) {
        strcpy(ip, s);
    } else {
        ip[0] = '\0';
    }
}

void nanoev_addr_get_port(
    const struct nanoev_addr *addr, 
    unsigned short *port
    )
{
    ASSERT(addr && port);
    *port = ntohs(addr->port);
}
