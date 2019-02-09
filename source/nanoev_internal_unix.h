#ifndef __NANOEV_INTERNAL_UNIX_H__
#define __NANOEV_INTERNAL_UNIX_H__

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>

typedef int SOCKET;

#define INVALID_SOCKET (-1)

typedef struct io_context {
    int status;
    int bytes;
} io_context;

typedef struct io_buf {
    unsigned int len;
    char *buf;
} io_buf;

#endif /* __NANOEV_INTERNAL_UNIX_H__ */
