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

#define d_force(f, args...) printf("\033[1m  %s:%d " f "\033[0m\n", __FILE__, __LINE__, ##args)

#if !defined(NDEBUG)

#define d_info(f, args...) printf("  %s:%d " f "\n", __FILE__, __LINE__, ##args)
#define d_warn(f, args...) printf("\033[33m  %s:%d " f "\033[0m\n", __FILE__, __LINE__, ##args)
#define d_err(f, args...) printf("\033[1;31m  %s:%d " f "\033[0m\n", __FILE__, __LINE__, ##args)

#else

#define d_info(f, args...) ((void) 0)
#define d_warn(f, args...) ((void) 0)
#define d_err(f, args...) ((void) 0)

#endif // NDEBUG

#endif // DEBUG_HPP
