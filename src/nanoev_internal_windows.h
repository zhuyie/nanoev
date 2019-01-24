#ifndef __NANOEV_INTERNAL_WINDOWS_H__
#define __NANOEV_INTERNAL_WINDOWS_H__

#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>

/*----------------------------------------------------------------------------*/

typedef int socklen_t;

typedef OVERLAPPED io_context;

typedef WSABUF io_buf;

typedef struct {
    LPFN_CONNECTEX ConnectEx;
    LPFN_ACCEPTEX AcceptEx;
} nanoev_winsock_ext;

const nanoev_winsock_ext* get_winsock_ext();

#if _WIN32_WINNT < 0x0600

#define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#define FILE_SKIP_SET_EVENT_ON_HANDLE        0x2

typedef struct _OVERLAPPED_ENTRY {
    ULONG_PTR lpCompletionKey;
    LPOVERLAPPED lpOverlapped;
    ULONG_PTR Internal;
    DWORD dwNumberOfBytesTransferred;
} OVERLAPPED_ENTRY, *LPOVERLAPPED_ENTRY;

#endif

typedef BOOL (WINAPI *PFN_GetQueuedCompletionStatusEx)(
    HANDLE CompletionPort,
    LPOVERLAPPED_ENTRY lpCompletionPortEntries,
    ULONG ulCount,
    PULONG ulNumEntriesRemoved,
    DWORD dwMilliseconds,
    BOOL fAlertable
    );
typedef BOOL (WINAPI *PFN_SetFileCompletionNotificationModes)(
    HANDLE FileHandle,
    UCHAR Flags
    );

typedef struct {
    PFN_GetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx;
    PFN_SetFileCompletionNotificationModes pSetFileCompletionNotificationModes;
} nanoev_win32_ext_fns;

const nanoev_win32_ext_fns* get_win32_ext_fns();

int  ntstatus_to_winsock_error(long status);

/*----------------------------------------------------------------------------*/

#endif /* __NANOEV_INTERNAL_WINDOWS_H__ */
