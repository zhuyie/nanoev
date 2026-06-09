#include "nanoev_poller.h"

/*----------------------------------------------------------------------------*/

extern poller_impl _nanoev_poller_impl;

poller_impl* get_poller_impl(void)
{
#ifdef _WIN32
    void init_iocp_poller_impl(void);
    init_iocp_poller_impl();
#endif
    return &_nanoev_poller_impl;
}

/*----------------------------------------------------------------------------*/
