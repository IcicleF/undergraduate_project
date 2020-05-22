/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(COMMONS_HPP)
#define COMMONS_HPP

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <unistd.h>

// C++ makes me happy
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>

#include "debug.hpp"

#define MAX_NODES               32              /* Maximum of number of nodes in cluster */ 

#define MAX_HOSTNAME_LEN        128             /* Maximum of host name length of all nodes */

#define ADDR_RESOLVE_TIMEOUT    3000            /* rdma_resolve_addr timeout in ms */
#define EC_POLL_TIMEOUT         10              /* rdma_event_channel poll timeout in ms */
#define MAX_REQS                16              /* rdma_accept initiator_depth */
#define MAX_QP_DEPTH            2048            /* Maximum QPEs/CQEs in QPs/CQs */
#define MAX_CQS                 2               /* Maximum of CQs held by a single node */
#define CQ_SEND                 0               /* # of CQ for ibv_post_send's */
#define CQ_RECV                 1               /* # of CQ for ibv_post_recv's */

#define WRITE_LOG_SIZE          50000           /* Max size of write log when degraded */

#define RDMA_BUF_SIZE           4096            /* RDMA send/recv memory buffer size */
#define ALLOC_TABLE_MAGIC       0xAB71E514      /* Allocation table magic number */

#define MAX_PATH_LEN            255             /* Max path length */ 

/* Flush cache line containing `addr`. */
#define __mem_clflush(addr)                 \
    asm volatile (                          \
        "clflush %0;"                       \
        "sfence"                            \
        : "+m"((addr))                      \
    )

#define Likely(x)               __builtin_expect(!!(x), 1)
#define Unlikely(x)             __builtin_expect(!!(x), 0)

#define COMBINE_I32(x, y)       ((((uint64_t)(x)) << 32) | ((uint64_t)(y)))
#define EXTRACT_X(u)            ((uint32_t)(((u) >> 32) & 0xFFFFFFFF))
#define EXTRACT_Y(u)            ((uint32_t)((u) & 0xFFFFFFFF))

#ifdef __packed
#undef __packed
#endif
#define __packed __attribute__((packed))

/* Stores thread ID of main thread to enable checks when joining */
extern std::thread::id mainThreadId;

/* Ensures that the persistent memory space is used only once */
extern std::atomic<bool> pmemOccupied;

/*
 * This function defines necessary information variables, and must
 * be called ONCE AND ONLY ONCE in the main function.
 */
#define DEFINE_MAIN_INFO()                          \
    std::thread::id mainThreadId;                   \
    std::atomic<bool> pmemOccupied;

/*
 * This macro collects necessary information about the environment,
 * and must be called before ANYTHING in Galois.
 */
#define COLLECT_MAIN_INFO()                         \
    do {                                            \
        mainThreadId = std::this_thread::get_id();  \
        pmemOccupied.store(false);                  \
    } while (0);

#endif // COMMONS_HPP
