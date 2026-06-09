#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

int nanoev_init(void)
{
    return global_init();
}

void nanoev_term(void)
{
    global_term();
}
