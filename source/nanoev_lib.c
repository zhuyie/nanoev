#include "nanoev_internal.h"

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
