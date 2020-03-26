/*
 * commons.hpp
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Defines necessary includes and constants.
 * All other headers (should) include this file.
 */

#if !defined(COMMONS_HPP)
#define COMMONS_HPP

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <unistd.h>

// C++ makes me happy
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
#define MAX_REQS                16              /* rdma_accept initiator_depth */
#define MAX_QP_DEPTH            1024            /* Maximum QPEs/CQEs in QPs/CQs */
#define MAX_CQS                 2               /* Maximum of CQs held by a single node */
#define CQ_SEND                 0               /* # of CQ for ibv_post_send's */
#define CQ_RECV                 1               /* # of CQ for ibv_post_recv's */

#define RDMA_BUF_SIZE           4096            /* RDMA send/recv memory buffer size */
#define ALLOC_TABLE_MAGIC       0xAB71E514      /* Allocation table magic number */

/* Flush cache line containing `addr`. */
#define __mem_clflush(addr)                 \
    asm volatile (                          \
        "clflush %0;"                       \
        "sfence"                            \
        : "+m"((addr))                      \
    )

#define likely(x)               __builtin_expect(!!(x), 1)
#define unlikely(x)             __builtin_expect(!!(x), 0)

#ifdef __packed
#undef __packed
#endif
#define __packed __attribute__((packed))

extern std::thread::id mainThreadId;
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