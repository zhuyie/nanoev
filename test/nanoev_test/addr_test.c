#include "nanoev.h"
#include <assert.h>
#include <string.h>

#define ASSERT assert

static void test_ipv4_port_zero()
{
    struct nanoev_addr addr;
    char ip[16];
    unsigned short port;

    ASSERT(nanoev_addr_init(&addr, NANOEV_AF_INET, "127.0.0.1", 0) == NANOEV_SUCCESS);
    ASSERT(nanoev_addr_get_ip(&addr, ip, sizeof(ip)) == NANOEV_SUCCESS);
    ASSERT(strcmp(ip, "127.0.0.1") == 0);
    ASSERT(nanoev_addr_get_port(&addr, &port) == NANOEV_SUCCESS);
    ASSERT(port == 0);
}

static void test_ipv6_port_zero()
{
    struct nanoev_addr addr;
    char ip[46];
    unsigned short port;

    ASSERT(nanoev_addr_init(&addr, NANOEV_AF_INET6, "::1", 0) == NANOEV_SUCCESS);
    ASSERT(nanoev_addr_get_ip(&addr, ip, sizeof(ip)) == NANOEV_SUCCESS);
    ASSERT(strcmp(ip, "::1") == 0);
    ASSERT(nanoev_addr_get_port(&addr, &port) == NANOEV_SUCCESS);
    ASSERT(port == 0);
}

static void test_invalid_input()
{
    struct nanoev_addr addr;
    char ip[16];
    unsigned short port;

    ASSERT(nanoev_addr_init(NULL, NANOEV_AF_INET, "127.0.0.1", 0) == NANOEV_ERROR_INVALID_ARG);
    ASSERT(nanoev_addr_init(&addr, NANOEV_AF_INET, NULL, 0) == NANOEV_ERROR_INVALID_ARG);
    ASSERT(nanoev_addr_init(&addr, NANOEV_AF_INET, "not-an-ip", 0) == NANOEV_ERROR_INVALID_ARG);
    ASSERT(nanoev_addr_init(&addr, 0, "127.0.0.1", 0) == NANOEV_ERROR_INVALID_ARG);

    ASSERT(nanoev_addr_init(&addr, NANOEV_AF_INET, "127.0.0.1", 80) == NANOEV_SUCCESS);
    ASSERT(nanoev_addr_get_ip(NULL, ip, sizeof(ip)) == NANOEV_ERROR_INVALID_ARG);
    ASSERT(nanoev_addr_get_ip(&addr, NULL, sizeof(ip)) == NANOEV_ERROR_INVALID_ARG);
    ASSERT(nanoev_addr_get_ip(&addr, ip, 1) == NANOEV_ERROR_INVALID_ARG);
    ASSERT(nanoev_addr_get_port(NULL, &port) == NANOEV_ERROR_INVALID_ARG);
    ASSERT(nanoev_addr_get_port(&addr, NULL) == NANOEV_ERROR_INVALID_ARG);
}

void test_addr()
{
    test_ipv4_port_zero();
    test_ipv6_port_zero();
    test_invalid_input();
}
