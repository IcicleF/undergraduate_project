/*
 * debug.hpp
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * One-line debugging macros to let users print messages to stdout.
 * These macros are equivalent to no-ops if NDEBUG is defined.
 */

#if !defined(DEBUG_HPP)
#define DEBUG_HPP

#include <cstdio>

#define d_force(f, args...) printf("\033[1m  %s:%d\t" f "\033[0m\n", __FILE__, __LINE__, ##args)

#if !defined(NDEBUG)

#define d_info(f, args...)  printf("  %s:%d\t" f "\n", __FILE__, __LINE__, ##args)
#define d_warn(f, args...)  printf("\033[33m  %s:%d\t" f "\033[0m\n", __FILE__, __LINE__, ##args)
#define d_err(f, args...)   printf("\033[1;31m  %s:%d\t" f "\033[0m\n", __FILE__, __LINE__, ##args)

#define expectZero(x)       do { if ((x)) { d_err(#x " failed (!= 0, %s)", strerror(errno)); } } while (0)
#define expectNonZero(x)    do { if (!(x)) { d_err(#x " failed (== 0, %s)", strerror(errno)); } } while (0)
#define expectTrue(x)       do { if (!(x)) { d_err(#x " failed (false, %s)", strerror(errno)); } } while (0)
#define expectFalse(x)      do { if ((x)) { d_err(#x " failed (true, %s)", strerror(errno)); } } while (0)
#define expectPositive(x)   do { if ((x) <= 0) { d_err(#x " failed (<= 0, %s)", strerror(errno)); } } while (0)
#define expectNegative(x)   do { if ((x) >= 0) { d_err(#x " failed (>= 0), %s", strerror(errno)); } } while (0)

#else

#define d_info(f, args...)  ((void) 0)
#define d_warn(f, args...)  ((void) 0)
#define d_err(f, args...)   ((void) 0)

#define expectZero(x)       x
#define expectNonZero(x)    x
#define expectTrue(x)       x
#define expectFalse(x)      x
#define expectPositive(x)   x
#define expectNegative(x)   x

#endif // NDEBUG

#endif // DEBUG_HPP
