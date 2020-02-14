#if !defined(ECAL_H)
#define ECAL_H

#include <libpmemblk.h>

#include "commons.h"

/**
 * ECAL: Erasure Coding Availability Layer
 * 
 * Provides block granularity access to `virtual` distributed NVM.
 */
struct ecal
{
    PMEMblkpool *pmem_blkpool;
};

int ecal_init(struct ecal *ecal, void *config);
int ecal_destroy(struct ecal *ecal);

int ecal_readblks(struct ecal *ecal, void *dest, int nblks);
int ecal_writeblks(struct ecal *ecal, void *src, int nblks);

#endif // ECAL_H
