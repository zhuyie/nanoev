// http_client.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "nanoev.hpp"

int main(int argc, char* argv[])
{
    int ret_code;
    nanoev_loop *loop;

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);

    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    nanoev_loop_free(loop);
    
    nanoev_term();

	return 0;
}

