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

#if !defined(__cplusplus)
#error use C++ compiler!
#endif

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

#define MAX_NODES               32
#define MAX_MDS_BAKS            3

#define MAX_HOSTNAME_LEN        128

#define ADDR_RESOLVE_TIMEOUT    3000
#define MAX_REQS                16
#define MAX_QP_DEPTH            64
#define MAX_DEST_RD_ATOMIC      16
#define MAX_CQS                 2
#define CQ_SEND                 0
#define CQ_RECV                 1
#define PSN_MAGIC               4396

#define RDMA_BUF_SIZE           4096
#define ALLOC_TABLE_MAGIC       0xAB71E514

#define __mem_clflush(addr)                 \
    asm volatile (                          \
        "clflush %0;"                       \
        "sfence"                            \
        : "+m"((addr))                      \
    )

#define likely(x)               __builtin_expect(!!(x), 1)
#define unlikely(x)             __builtin_expect(!!(x), 0)

#define expectZero(x)           do { if ((x)) { d_err(#x "  failed (!= 0, %s)", strerror(errno)); } } while (0)
#define expectNonZero(x)        do { if (!(x)) { d_err(#x "  failed (== 0, %s)", strerror(errno)); } } while (0)
#define expectTrue(x)           do { if (!(x)) { d_err(#x "  failed (false, %s)", strerror(errno)); } } while (0)
#define expectFalse(x)          do { if ((x)) { d_err(#x "  failed (true, %s)", strerror(errno)); } } while (0)
#define expectPositive(x)       do { if ((x) <= 0) { d_err(#x "  failed (<= 0, %s)", strerror(errno)); } } while (0)
#define expectNegative(x)       do { if ((x) >= 0) { d_err(#x "  failed (>= 0), %s", strerror(errno)); } } while (0)

#ifdef __packed
#undef __packed
#endif
#define __packed __attribute__((packed))

extern std::thread::id mainThreadId;

/*
 * This function defines necessary information variables, and must
 * be called ONCE AND ONLY ONCE in the main function.
 */
#define DEFINE_MAIN_INFO()                          \
    std::thread::id mainThreadId;

/*
 * This macro collects necessary information about the environment,
 * and must be called before ANYTHING in Galois.
 */
#define COLLECT_MAIN_INFO()                         \
    do {                                            \
        mainThreadId = std::this_thread::get_id();  \
    } while (0);

#endif // COMMONS_HPP
