/*
 * ecal.h
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Implements a midware to provide highly-available access to distributed NVM.
 * The LRC (Local Reconstruction Code) erasure coding is used to provide availability.
 * Provides block-granularity access to higher levels.
 * 
 * Based on ISA-L & libpmemblk.
 */

#if !defined(ECAL_H)
#define ECAL_H

#include <isa-l.h>
#include <libpmemblk.h>

#include "common.h"

struct ecal
{
    PMEMblkpool *pmem_blkpool;
};

int ecal_init(struct ecal *ecal, void *config);
int ecal_destroy(struct ecal *ecal);

int ecal_readblks(struct ecal *ecal, void *dest, int nblks);
int ecal_writeblks(struct ecal *ecal, void *src, int nblks);

#endif // ECAL_H
