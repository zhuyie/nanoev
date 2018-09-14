#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

int global_init()
{
    return NANOEV_SUCCESS;
}

void global_term()
{
}

/*----------------------------------------------------------------------------*/

void time_now(struct nanoev_timeval *tv)
{
    gettimeofday((struct timeval*)tv, NULL);
}
