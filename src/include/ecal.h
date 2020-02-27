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
#include <libpmem.h>

#include "cluster.h"
#include "alloctable.h"

struct ecal
{
    struct mem_config *mem_conf;

    struct alloc_table *metadata;
    void *data_blks;

    struct rdma_resource rs;
};

int ecal_init(struct ecal *ecal);
int ecal_destroy(struct ecal *ecal);

int ecal_readblks(struct ecal *ecal, void *dest, int offset, int nblks);
int ecal_writeblks(struct ecal *ecal, void *src, int offset, int nblks);

#endif // ECAL_H
