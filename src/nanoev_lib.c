#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

static nanoev_winsock_ext winsock_ext = { 0 };
static int init_winsock_ext();

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
