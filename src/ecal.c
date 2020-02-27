#include <stdio.h>
#include <stdlib.h>

#include <ecal.h>

int ecal_init(struct ecal *ecal)
{
    if (ecal->mem_conf == NULL)
        return -1;

    // TODO: mem_conf should be inited

    // TODO: calc alloc_table & data_blks
    

    return 0;
}
