#include <ntstatus.h>
#define WIN32_NO_STATUS
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

#define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64

void nanoev_now(struct nanoev_timeval *tv)
{
    FILETIME ft;
    unsigned __int64 tmpres = 0;

    GetSystemTimeAsFileTime(&ft);

    tmpres = ft.dwHighDateTime;
    tmpres <<= 32;
    tmpres |= ft.dwLowDateTime;

    /* convert into microseconds */
    tmpres /= 10;
    /* converting file time to unix epoch */
    tmpres -= DELTA_EPOCH_IN_MICROSECS; 

    tv->tv_sec = (unsigned int)(tmpres / 1000000UL);
    tv->tv_usec = (unsigned int)(tmpres % 1000000UL);
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

#define NTSTATUS LONG

#ifndef STATUS_HOPLIMIT_EXCEEDED
#define STATUS_HOPLIMIT_EXCEEDED ((NTSTATUS) 0xC000A012L)
#endif

#ifndef FACILITY_NTWIN32
#define FACILITY_NTWIN32 0x7
#endif

int ntstatus_to_winsock_error(long status)
{
    switch (status) {
    case STATUS_SUCCESS:
        return ERROR_SUCCESS;

    case STATUS_PENDING:
        return ERROR_IO_PENDING;

    case STATUS_INVALID_HANDLE:
    case STATUS_OBJECT_TYPE_MISMATCH:
        return WSAENOTSOCK;

    case STATUS_INSUFFICIENT_RESOURCES:
    case STATUS_PAGEFILE_QUOTA:
    case STATUS_COMMITMENT_LIMIT:
    case STATUS_WORKING_SET_QUOTA:
    case STATUS_NO_MEMORY:
    case STATUS_CONFLICTING_ADDRESSES:
    case STATUS_QUOTA_EXCEEDED:
    case STATUS_TOO_MANY_PAGING_FILES:
    case STATUS_REMOTE_RESOURCES:
    case STATUS_TOO_MANY_ADDRESSES:
        return WSAENOBUFS;

    case STATUS_SHARING_VIOLATION:
    case STATUS_ADDRESS_ALREADY_EXISTS:
        return WSAEADDRINUSE;

    case STATUS_LINK_TIMEOUT:
    case STATUS_IO_TIMEOUT:
    case STATUS_TIMEOUT:
        return WSAETIMEDOUT;

    case STATUS_GRACEFUL_DISCONNECT:
        return WSAEDISCON;

    case STATUS_REMOTE_DISCONNECT:
    case STATUS_CONNECTION_RESET:
    case STATUS_LINK_FAILED:
    case STATUS_CONNECTION_DISCONNECTED:
    case STATUS_PORT_UNREACHABLE:
    case STATUS_HOPLIMIT_EXCEEDED:
        return WSAECONNRESET;

    case STATUS_LOCAL_DISCONNECT:
    case STATUS_TRANSACTION_ABORTED:
    case STATUS_CONNECTION_ABORTED:
        return WSAECONNABORTED;

    case STATUS_BAD_NETWORK_PATH:
    case STATUS_NETWORK_UNREACHABLE:
    case STATUS_PROTOCOL_UNREACHABLE:
        return WSAENETUNREACH;

    case STATUS_HOST_UNREACHABLE:
        return WSAEHOSTUNREACH;

    case STATUS_CANCELLED:
    case STATUS_REQUEST_ABORTED:
        return WSAEINTR;

    case STATUS_BUFFER_OVERFLOW:
    case STATUS_INVALID_BUFFER_SIZE:
        return WSAEMSGSIZE;

    case STATUS_BUFFER_TOO_SMALL:
    case STATUS_ACCESS_VIOLATION:
        return WSAEFAULT;

    case STATUS_DEVICE_NOT_READY:
    case STATUS_REQUEST_NOT_ACCEPTED:
        return WSAEWOULDBLOCK;

    case STATUS_INVALID_NETWORK_RESPONSE:
    case STATUS_NETWORK_BUSY:
    case STATUS_NO_SUCH_DEVICE:
    case STATUS_NO_SUCH_FILE:
    case STATUS_OBJECT_PATH_NOT_FOUND:
    case STATUS_OBJECT_NAME_NOT_FOUND:
    case STATUS_UNEXPECTED_NETWORK_ERROR:
        return WSAENETDOWN;

    case STATUS_INVALID_CONNECTION:
        return WSAENOTCONN;

    case STATUS_REMOTE_NOT_LISTENING:
    case STATUS_CONNECTION_REFUSED:
        return WSAECONNREFUSED;

    case STATUS_PIPE_DISCONNECTED:
        return WSAESHUTDOWN;

    case STATUS_INVALID_ADDRESS:
    case STATUS_INVALID_ADDRESS_COMPONENT:
        return WSAEADDRNOTAVAIL;

    case STATUS_NOT_SUPPORTED:
    case STATUS_NOT_IMPLEMENTED:
        return WSAEOPNOTSUPP;

    case STATUS_ACCESS_DENIED:
        return WSAEACCES;

    default:
        if ((status & (FACILITY_NTWIN32 << 16)) == (FACILITY_NTWIN32 << 16)
            && (status & (ERROR_SEVERITY_ERROR | ERROR_SEVERITY_WARNING))
            ) {
                /* It's a windows error that has been previously mapped to an ntstatus code. */
                return (DWORD) (status & 0xffff);
        } else {
            /* The default fallback for unmappable ntstatus codes. */
            return WSAEINVAL;
        }
    }
}
