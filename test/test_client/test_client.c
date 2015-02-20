#include <stdio.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

int main(int argc, char* argv[])
{
    int ret_code;
    nanoev_loop *loop;

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);

    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    ret_code = nanoev_loop_run(loop);
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    nanoev_loop_free(loop);

    nanoev_term();

    return 0;
}
