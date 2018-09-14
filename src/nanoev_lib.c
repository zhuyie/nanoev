#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

int nanoev_init()
{
    return global_init();
}

void nanoev_term()
{
    global_term();
}
