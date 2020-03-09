/*
 * common.h
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Defines necessary includes and constants.
 * All other headers (should) include this file.
 */

#if !defined(COMMONS_HPP)
#define COMMONS_HPP

#if defined(__cplusplus)
    #if (__cplusplus < 201703L)
    #error compile this program in C++17 environment!
    #endif
#else
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
#include <optional>

#define MAX_NODES               32
#define MAX_MDS_BAKS            3

#define MAX_HOSTNAME_LEN        128
#define MAX_CONN_RETRIES        3
#define CONN_RETRY_INTERVAL     (1000 * 1000)
#define MAX_QUEUED_CONNS        5

#define MAX_QP_DEPTH            64
#define MAX_DEST_RD_ATOMIC      16
#define PSN_MAGIC               4396

#define RDMA_BUF_SIZE           4096
#define ALLOC_TABLE_MAGIC       0xAB71E514


#define mem_force_flush(addr)               \
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

#endif // COMMONS_HPP