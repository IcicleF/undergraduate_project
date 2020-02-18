#if !defined(DEBUG_H)
#define DEBUG_H

#if !defined(NDEBUG)

#include <stdio.h>

#define d_info(f, args...) printf("\033[1m  %s:%d " f "\033[0m\n", __FILE__, __LINE__, ##args)
#define d_warn(f, args...) printf("\033[1;33m  %s:%d " f "\033[0m\n", __FILE__, __LINE__, ##args)
#define d_err(f, args...) printf("\033[1;31m  %s:%d " f "\033[0m\n", __FILE__, __LINE__, ##args)

#else

#define d_info(f, args...) ((void) 0)
#define d_warn(f, args...) ((void) 0)
#define d_err(f, args...) ((void) 0)

#endif // NDEBUG

#endif // DEBUG_H
