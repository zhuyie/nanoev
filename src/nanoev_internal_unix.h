#ifndef __NANOEV_INTERNAL_UNIX_H__
#define __NANOEV_INTERNAL_UNIX_H__

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>

typedef int SOCKET;

typedef struct _OVERLAPPED {
    int reserved;
} OVERLAPPED, *LPOVERLAPPED;

#endif /* __NANOEV_INTERNAL_UNIX_H__ */