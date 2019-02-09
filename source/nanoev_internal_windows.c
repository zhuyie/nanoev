#include <ntstatus.h>
#define WIN32_NO_STATUS
#include "nanoev_internal.h"
#include <stdio.h>

/*----------------------------------------------------------------------------*/

static nanoev_winsock_ext winsock_ext = { 0 };
static nanoev_win32_ext_fns win32_ext_fns = { 0 };
static int init_winsock_ext();
static void init_win32_ext_fns();

int global_init()
{
    WSADATA wsa_data;

    /* init Winsock */
    if (0 != WSAStartup(MAKEWORD(2, 2), &wsa_data))
        return NANOEV_ERROR_FAIL;

    if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2)
        goto ERROR_EXIT;

    /* init Winsock extension function table */
    if (init_winsock_ext())
        goto ERROR_EXIT;

    /* init Win32 extension function table */
    init_win32_ext_fns();

    return NANOEV_SUCCESS;

ERROR_EXIT:
    WSACleanup();
    return NANOEV_ERROR_FAIL;
}

void global_term()
{
    /* term Winsock */
    WSACleanup();
}

static int init_winsock_ext()
{
    DWORD cbReturn;
    GUID guidCONNECTEX = WSAID_CONNECTEX;
    GUID guidACCEPTEX = WSAID_ACCEPTEX;
    SOCKET s;

    if (winsock_ext.ConnectEx && winsock_ext.AcceptEx)
        return 0;

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return -1;

    WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidCONNECTEX, sizeof(GUID), 
        &(winsock_ext.ConnectEx), sizeof(winsock_ext.ConnectEx), &cbReturn, NULL, NULL);
    WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidACCEPTEX, sizeof(GUID), 
        &(winsock_ext.AcceptEx), sizeof(winsock_ext.AcceptEx), &cbReturn, NULL, NULL);

    closesocket(s);

    if (!winsock_ext.ConnectEx || !winsock_ext.AcceptEx)
        return -1;

    return 0;
}

static void init_win32_ext_fns()
{
    HMODULE hDLL = GetModuleHandleW(L"kernel32.dll");
    if (!hDLL)
        return;

    win32_ext_fns.pGetQueuedCompletionStatusEx = 
        (PFN_GetQueuedCompletionStatusEx)GetProcAddress(hDLL, "GetQueuedCompletionStatusEx");
    win32_ext_fns.pSetFileCompletionNotificationModes = 
        (PFN_SetFileCompletionNotificationModes)GetProcAddress(hDLL, "SetFileCompletionNotificationModes");
}

/*----------------------------------------------------------------------------*/

int  mutex_init(mutex *m)
{
    InitializeCriticalSection(m);
    return 0;
}

void mutex_uninit(mutex *m)
{
    DeleteCriticalSection(m);
}

void mutex_lock(mutex *m)
{
    EnterCriticalSection(m);
}

void mutex_unlock(mutex *m)
{
    LeaveCriticalSection(m);
}

/*----------------------------------------------------------------------------*/

thread_t get_current_thread()
{
    return GetCurrentThreadId();
}

/*----------------------------------------------------------------------------*/

#define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64

void time_now(nanoev_timeval *tv)
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

/*----------------------------------------------------------------------------*/

const nanoev_winsock_ext* get_winsock_ext()
{
    return &winsock_ext;
}

const nanoev_win32_ext_fns* get_win32_ext_fns()
{
    return &win32_ext_fns;
}

/*----------------------------------------------------------------------------*/

int set_non_blocking(SOCKET sock, int set)
{
    // Set the socket I/O mode: In this case FIONBIO
    // enables or disables the blocking mode for the 
    // socket based on the numerical value of iMode.
    // If iMode = 0, blocking is enabled; 
    // If iMode != 0, non-blocking mode is enabled.
    unsigned long mode = set ? 1 : 0;
    return (ioctlsocket(sock, FIONBIO, &mode) == 0) ? 1 : 0;
}

void close_socket(SOCKET sock)
{
    closesocket(sock);
}

int socket_last_error()
{
    return WSAGetLastError();
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

/*----------------------------------------------------------------------------*/

static ULONG PipeSerialNumber;

BOOL CreatePipeEx(
    LPHANDLE lpReadPipe,
    LPHANDLE lpWritePipe,
    LPSECURITY_ATTRIBUTES lpPipeAttributes,
    DWORD nSize,
    DWORD dwReadMode,
    DWORD dwWriteMode
    )
/*++
Routine Description:
    The CreatePipeEx API is used to create an anonymous pipe I/O device.
    Unlike CreatePipe FILE_FLAG_OVERLAPPED may be specified for one or
    both handles.
    Two handles to the device are created.  One handle is opened for
    reading and the other is opened for writing.  These handles may be
    used in subsequent calls to ReadFile and WriteFile to transmit data
    through the pipe.
Arguments:
    lpReadPipe - Returns a handle to the read side of the pipe.  Data
        may be read from the pipe by specifying this handle value in a
        subsequent call to ReadFile.
    lpWritePipe - Returns a handle to the write side of the pipe.  Data
        may be written to the pipe by specifying this handle value in a
        subsequent call to WriteFile.
    lpPipeAttributes - An optional parameter that may be used to specify
        the attributes of the new pipe.  If the parameter is not
        specified, then the pipe is created without a security
        descriptor, and the resulting handles are not inherited on
        process creation.  Otherwise, the optional security attributes
        are used on the pipe, and the inherit handles flag effects both
        pipe handles.
    nSize - Supplies the requested buffer size for the pipe.  This is
        only a suggestion and is used by the operating system to
        calculate an appropriate buffering mechanism.  A value of zero
        indicates that the system is to choose the default buffering
        scheme.
Return Value:
    TRUE - The operation was successful.
    FALSE/NULL - The operation failed. Extended error status is available
        using GetLastError.
--*/
{
    HANDLE ReadPipeHandle, WritePipeHandle;
    DWORD dwError;
    CHAR PipeNameBuffer[MAX_PATH];

    //
    // Only one valid OpenMode flag - FILE_FLAG_OVERLAPPED
    //
    if ((dwReadMode | dwWriteMode) & (~FILE_FLAG_OVERLAPPED)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (nSize == 0) {
        nSize = 4096;
    }

    sprintf(PipeNameBuffer,
        "\\\\.\\Pipe\\nanoev.%08x.%08x",
        GetCurrentProcessId(),
        InterlockedIncrement(&PipeSerialNumber)
        );

    ReadPipeHandle = CreateNamedPipeA(
        PipeNameBuffer,
        PIPE_ACCESS_INBOUND | dwReadMode,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1,             // Number of pipes
        nSize,         // Out buffer size
        nSize,         // In buffer size
        120 * 1000,    // Timeout in ms
        lpPipeAttributes
        );
    if (!ReadPipeHandle) {
        return FALSE;
    }

    WritePipeHandle = CreateFileA(
        PipeNameBuffer,
        GENERIC_WRITE,
        0,                         // No sharing
        lpPipeAttributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | dwWriteMode,
        NULL                       // Template file
        );
    if (INVALID_HANDLE_VALUE == WritePipeHandle) {
        dwError = GetLastError();
        CloseHandle(ReadPipeHandle);
        SetLastError(dwError);
        return FALSE;
    }

    *lpReadPipe = ReadPipeHandle;
    *lpWritePipe = WritePipeHandle;
    return TRUE;
}
