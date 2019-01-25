#include "nanoev_poller.h"

/*----------------------------------------------------------------------------*/

poller_impl* get_poller_impl()
{
#ifdef _WIN32
    void init_iocp_poller_impl();
    extern poller_impl iocp_poller_impl;

    init_iocp_poller_impl();
    return &iocp_poller_impl;
#endif
	return NULL;
}

/*----------------------------------------------------------------------------*/
