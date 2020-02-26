/*
 * common.h
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Defines necessary includes and constants.
 * All other headers (should) include this file.
 */

#if !defined(COMMONS_H)
#define COMMONS_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <memory.h>
#include <malloc.h>
#include <errno.h> 

#define MAX_NODES               32
#define MAX_PEERS               MAX_NODES
#define MAX_MDS_BAKS            3

#define MAX_HOSTNAME_LEN        128
#define MAX_CONN_RETRIES        3
#define CONN_RETRY_INTERVAL     (1000 * 1000)
#define MAX_QUEUED_CONNS        5

#define MAX_QP_DEPTH            64
#define MAX_DEST_RD_ATOMIC      16
#define PSN_MAGIC               4396

#define RDMA_RECV_BUF_SIZE      4096

#define NODE_TYPE_CM            0x10            // Cluster Manager
#define NODE_TYPE_MDS           0x20            // Metadata Server (Centralized)
#define NODE_TYPE_MDS_BAK       0x21            // Metadata Server (Slave Replication)
#define NODE_TYPE_DS            0x30            // Data Server
#define NODE_TYPE_CLI           0x40            // Client
#define MAX_NODE_TYPE_LEN       8

#define ALLOC_TABLE_MAGIC       0xAB71E514


#define mem_force_flush(addr)               \
    asm volatile (                          \
        "clflush %0;"                       \
        "sfence"                            \
        : "+m"((addr))                      \
    )

#define likely(x)               __builtin_expect(!!(x), 1)
#define unlikely(x)             __builtin_expect(!!(x), 0)

#endif // COMMONS_H
