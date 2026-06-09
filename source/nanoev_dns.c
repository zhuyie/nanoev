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
    struct nanoev_dns *queue_next;
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

#define NANOEV_DNS_FLAG_DELETED  0x80000000
#define DNS_THREAD_COUNT         4

typedef struct dns_thread_pool {
    mutex lock;
    cond ready;
    thread_handle threads[DNS_THREAD_COUNT];
    nanoev_dns *head;
    nanoev_dns *tail;
    int initialized;
    int started;
    int stopping;
} dns_thread_pool;

static void dns_destroy(nanoev_dns *dns);
static void dns_on_async(nanoev_event *async);
static void dns_worker(void *arg);
static int dns_start_workers_locked(void);
static int dns_enqueue(nanoev_dns *dns);
static char* dns_strdup(const char *str);
static int dns_copy_results(struct addrinfo *results, struct nanoev_addr **addrs, unsigned int *addr_count);

static dns_thread_pool dns_pool;

/*----------------------------------------------------------------------------*/

int dns_init(void)
{
    memset(&dns_pool, 0, sizeof(dns_pool));

    if (mutex_init(&dns_pool.lock))
        return NANOEV_ERROR_FAIL;
    if (cond_init(&dns_pool.ready)) {
        mutex_uninit(&dns_pool.lock);
        return NANOEV_ERROR_FAIL;
    }

    dns_pool.initialized = 1;
    return NANOEV_SUCCESS;
}

void dns_term(void)
{
    if (!dns_pool.initialized)
        return;

    mutex_lock(&dns_pool.lock);
    dns_pool.stopping = 1;
    cond_broadcast(&dns_pool.ready);
    mutex_unlock(&dns_pool.lock);

    while (dns_pool.started > 0) {
        dns_pool.started--;
        thread_join(dns_pool.threads[dns_pool.started]);
    }

    cond_uninit(&dns_pool.ready);
    mutex_uninit(&dns_pool.lock);
    memset(&dns_pool, 0, sizeof(dns_pool));
}

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

    if (dns_enqueue(dns) != NANOEV_SUCCESS) {
        mutex_lock(&dns->lock);
        dns->pending = 0;
        dns->host = NULL;
        dns->callback = NULL;
        mutex_unlock(&dns->lock);
        mem_free(host_copy);
        return NANOEV_ERROR_FAIL;
    }

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
    dns_thread_pool *pool = (dns_thread_pool*)arg;
    nanoev_dns *dns;
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct nanoev_addr *addrs = NULL;
    unsigned int addr_count = 0;
    char service[6];
    char *host;
    int family;
    unsigned short port;
    int status;

    while (1) {
        mutex_lock(&pool->lock);
        while (!pool->head && !pool->stopping) {
            cond_wait(&pool->ready, &pool->lock);
        }
        if (!pool->head && pool->stopping) {
            mutex_unlock(&pool->lock);
            return;
        }

        dns = pool->head;
        pool->head = dns->queue_next;
        if (!pool->head) {
            pool->tail = NULL;
        }
        dns->queue_next = NULL;
        mutex_unlock(&pool->lock);

        results = NULL;
        addrs = NULL;
        addr_count = 0;

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
}

static int dns_start_workers_locked(void)
{
    int i;

    for (i = 0; i < DNS_THREAD_COUNT; i++) {
        if (thread_create(&dns_pool.threads[i], dns_worker, &dns_pool) != NANOEV_SUCCESS) {
            dns_pool.stopping = 1;
            cond_broadcast(&dns_pool.ready);
            mutex_unlock(&dns_pool.lock);

            while (dns_pool.started > 0) {
                dns_pool.started--;
                thread_join(dns_pool.threads[dns_pool.started]);
            }

            mutex_lock(&dns_pool.lock);
            dns_pool.stopping = 0;
            return NANOEV_ERROR_FAIL;
        }
        dns_pool.started++;
    }

    return NANOEV_SUCCESS;
}

static int dns_enqueue(nanoev_dns *dns)
{
    mutex_lock(&dns_pool.lock);
    if (!dns_pool.initialized || dns_pool.stopping) {
        mutex_unlock(&dns_pool.lock);
        return NANOEV_ERROR_ACCESS_DENIED;
    }
    if (!dns_pool.started && dns_start_workers_locked() != NANOEV_SUCCESS) {
        mutex_unlock(&dns_pool.lock);
        return NANOEV_ERROR_FAIL;
    }

    dns->queue_next = NULL;
    if (dns_pool.tail) {
        dns_pool.tail->queue_next = dns;
    } else {
        dns_pool.head = dns;
    }
    dns_pool.tail = dns;
    cond_signal(&dns_pool.ready);
    mutex_unlock(&dns_pool.lock);

    return NANOEV_SUCCESS;
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
