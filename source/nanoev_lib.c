#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

const char* nanoev_strerror(
    int error_code
    )
{
    switch (error_code) {
    case NANOEV_SUCCESS:
        return "success";
    case NANOEV_ERROR_INVALID_ARG:
        return "nanoev error: invalid argument";
    case NANOEV_ERROR_ACCESS_DENIED:
        return "nanoev error: access denied";
    case NANOEV_ERROR_OUT_OF_MEMORY:
        return "nanoev error: out of memory";
    case NANOEV_ERROR_FAIL:
        return "nanoev error: operation failed";
    default:
        return "nanoev error: unknown";
    }
}

/*----------------------------------------------------------------------------*/

int nanoev_init(void)
{
    int ret = global_init();
    if (ret != NANOEV_SUCCESS)
        return ret;

    ret = dns_init();
    if (ret != NANOEV_SUCCESS) {
        global_term();
        return ret;
    }

    return NANOEV_SUCCESS;
}

void nanoev_term(void)
{
    dns_term();
    global_term();
}
