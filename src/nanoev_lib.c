#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

static nanoev_winsock_ext winsock_ext = { 0 };
static nanoev_win32_ext_fns win32_ext_fns = { 0 };
static int init_winsock_ext();
static void init_win32_ext_fns();

int nanoev_init()
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

void nanoev_term()
{
    /* term Winsock */
    WSACleanup();
}

const nanoev_winsock_ext* get_winsock_ext()
{
    return &winsock_ext;
}

const nanoev_win32_ext_fns* get_win32_ext_fns()
{
    return &win32_ext_fns;
}

/*----------------------------------------------------------------------------*/

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
