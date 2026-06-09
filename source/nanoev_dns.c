#include "nanoev_internal.h"

#include <stdio.h>
#ifdef _WIN32
# include <ws2tcpip.h>
#else
# include <netdb.h>
#endif

/*----------------------------------------------------------------------------*/

struct nanoev_dns {
    NANOEV_EVENT_FILEDS
    nanoev_event *async;
    mutex lock;
    int pending;
    int completed;
    int status;
    int family;
    unsigned short port;
    char *host;
    struct nanoev_addr *addrs;
    unsigned int addr_count;
    nanoev_dns_callback callback;
};
typedef struct nanoev_dns nanoev_dns;

static void dns_destroy(nanoev_dns *dns);
static void dns_on_async(nanoev_event *async);
static void dns_worker(void *arg);
static char* dns_strdup(const char *str);
static int dns_copy_results(struct addrinfo *results, struct nanoev_addr **addrs, unsigned int *addr_count);

#define NANOEV_DNS_FLAG_DELETED  0x80000000

/*----------------------------------------------------------------------------*/

nanoev_event* dns_new(nanoev_loop *loop, void *userdata)
{
    nanoev_dns *dns;

    dns = (nanoev_dns*)mem_alloc(sizeof(nanoev_dns));
    if (!dns)
        return NULL;

    memset(dns, 0, sizeof(nanoev_dns));
    dns->type = nanoev_event_dns;
    dns->loop = loop;
    dns->userdata = userdata;

    if (mutex_init(&dns->lock)) {
        mem_free(dns);
        return NULL;
    }

    dns->async = async_new(loop, dns);
    if (!dns->async) {
        mutex_uninit(&dns->lock);
        mem_free(dns);
        return NULL;
    }

    if (nanoev_async_start(dns->async, dns_on_async) != NANOEV_SUCCESS) {
        nanoev_event_free(dns->async);
        mutex_uninit(&dns->lock);
        mem_free(dns);
        return NULL;
    }

    return (nanoev_event*)dns;
}

void dns_free(nanoev_event *event)
{
    nanoev_dns *dns = (nanoev_dns*)event;
    int pending;

    ASSERT(dns);
    ASSERT(dns->type == nanoev_event_dns);

    mutex_lock(&dns->lock);
    pending = dns->pending;
    if (pending) {
        dns->flags |= NANOEV_DNS_FLAG_DELETED;
    }
    mutex_unlock(&dns->lock);

    if (!pending) {
        dns_destroy(dns);
    }
}

int nanoev_dns_resolve(
    nanoev_event *event,
    const char *host,
    int family,
    unsigned short port,
    nanoev_dns_callback callback
    )
{
    nanoev_dns *dns = (nanoev_dns*)event;
    thread_handle thread;
    char *host_copy;

    ASSERT(dns);
    ASSERT(dns->type == nanoev_event_dns);
    ASSERT(in_loop_thread(dns->loop));

    if (!host || !callback)
        return NANOEV_ERROR_INVALID_ARG;
    if (family != NANOEV_AF_UNSPEC && family != NANOEV_AF_INET && family != NANOEV_AF_INET6)
        return NANOEV_ERROR_INVALID_ARG;

    mutex_lock(&dns->lock);
    if (dns->pending || dns->flags & NANOEV_DNS_FLAG_DELETED) {
        mutex_unlock(&dns->lock);
        return NANOEV_ERROR_ACCESS_DENIED;
    }
    mutex_unlock(&dns->lock);

    host_copy = dns_strdup(host);
    if (!host_copy)
        return NANOEV_ERROR_OUT_OF_MEMORY;

    mutex_lock(&dns->lock);
    dns->pending = 1;
    dns->completed = 0;
    dns->status = 0;
    dns->family = family;
    dns->port = port;
    dns->host = host_copy;
    dns->addrs = NULL;
    dns->addr_count = 0;
    dns->callback = callback;
    mutex_unlock(&dns->lock);

    if (thread_create(&thread, dns_worker, dns) != NANOEV_SUCCESS) {
        mutex_lock(&dns->lock);
        dns->pending = 0;
        dns->host = NULL;
        dns->callback = NULL;
        mutex_unlock(&dns->lock);
        mem_free(host_copy);
        return NANOEV_ERROR_FAIL;
    }
    thread_detach(thread);

    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

static void dns_destroy(nanoev_dns *dns)
{
    if (dns->async) {
        nanoev_event_free(dns->async);
        dns->async = NULL;
    }
    if (dns->host) {
        mem_free(dns->host);
        dns->host = NULL;
    }
    if (dns->addrs) {
        mem_free(dns->addrs);
        dns->addrs = NULL;
    }
    mutex_uninit(&dns->lock);
    mem_free(dns);
}

static void dns_on_async(nanoev_event *async)
{
    nanoev_dns *dns = (nanoev_dns*)nanoev_event_userdata(async);
    nanoev_dns_callback callback;
    struct nanoev_addr *addrs;
    unsigned int addr_count;
    int status;
    int deleted;
    char *host;

    mutex_lock(&dns->lock);
    if (!dns->completed) {
        mutex_unlock(&dns->lock);
        return;
    }

    callback = dns->callback;
    status = dns->status;
    addrs = dns->addrs;
    addr_count = dns->addr_count;
    host = dns->host;
    deleted = (dns->flags & NANOEV_DNS_FLAG_DELETED) != 0;

    dns->pending = 0;
    dns->completed = 0;
    dns->callback = NULL;
    dns->addrs = NULL;
    dns->addr_count = 0;
    dns->host = NULL;
    mutex_unlock(&dns->lock);

    if (host) {
        mem_free(host);
    }

    if (!deleted && callback) {
        callback((nanoev_event*)dns, status, addrs, addr_count);
    }

    if (addrs) {
        mem_free(addrs);
    }

    if (deleted) {
        dns_destroy(dns);
    }
}

static void dns_worker(void *arg)
{
    nanoev_dns *dns = (nanoev_dns*)arg;
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct nanoev_addr *addrs = NULL;
    unsigned int addr_count = 0;
    char service[6];
    char *host;
    int family;
    unsigned short port;
    int status;

    mutex_lock(&dns->lock);
    host = dns->host;
    family = dns->family;
    port = dns->port;
    mutex_unlock(&dns->lock);

    snprintf(service, sizeof(service), "%u", (unsigned int)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo(host, service, &hints, &results);
    if (status == 0) {
        status = dns_copy_results(results, &addrs, &addr_count);
        if (status != 0) {
            if (addrs) {
                mem_free(addrs);
                addrs = NULL;
            }
            addr_count = 0;
        }
    }

    if (results) {
        freeaddrinfo(results);
    }

    mutex_lock(&dns->lock);
    dns->status = status;
    dns->addrs = addrs;
    dns->addr_count = addr_count;
    dns->completed = 1;
    mutex_unlock(&dns->lock);

    nanoev_async_send(dns->async);
}

static char* dns_strdup(const char *str)
{
    size_t len = strlen(str) + 1;
    char *copy = (char*)mem_alloc(len);
    if (!copy)
        return NULL;
    memcpy(copy, str, len);
    return copy;
}

static int dns_copy_results(struct addrinfo *results, struct nanoev_addr **addrs, unsigned int *addr_count)
{
    struct addrinfo *ai;
    struct nanoev_addr *copy;
    unsigned int count = 0;
    unsigned int index = 0;

    for (ai = results; ai; ai = ai->ai_next) {
        if ((ai->ai_family == AF_INET || ai->ai_family == AF_INET6)
            && ai->ai_addrlen <= sizeof(struct nanoev_addr)) {
            count++;
        }
    }

    if (!count)
        return NANOEV_ERROR_FAIL;

    copy = (struct nanoev_addr*)mem_alloc(sizeof(struct nanoev_addr) * count);
    if (!copy)
        return NANOEV_ERROR_OUT_OF_MEMORY;

    for (ai = results; ai; ai = ai->ai_next) {
        if ((ai->ai_family == AF_INET || ai->ai_family == AF_INET6)
            && ai->ai_addrlen <= sizeof(struct nanoev_addr)) {
            memset(&copy[index], 0, sizeof(struct nanoev_addr));
            memcpy(&copy[index], ai->ai_addr, ai->ai_addrlen);
            index++;
        }
    }

    *addrs = copy;
    *addr_count = count;
    return NANOEV_SUCCESS;
}
