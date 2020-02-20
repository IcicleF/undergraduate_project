/*
 * cluster.h
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Controls THIS node's role in the cluster.
 */

#if !defined(CLUSTER_H)
#define CLUSTER_H

#include "config.h"
#include "rdma.h"

int init_self(struct rdma_resource *rs, struct all_configs *conf);
int destroy_self(struct rdma_resource *rs, struct all_configs *conf);

#endif // CLUSTER_H
