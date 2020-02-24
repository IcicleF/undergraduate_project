#include <stdio.h>
#include <stdlib.h>

#include <alloc_table.h>
#include <debug.h>

/**
 * Initialize the allocation table (ALT).
 * 
 * Memory Organization:
 * +-------+----------+--------------------------------+
 * | Magic |  Bitmap  |          Element pool          |
 * +-------+----------+--------------------------------+
 * \                                                  /
 *  \____________________ area ______________________/
 * 
 * This function automacally calculates the element pool's length (= #bits in Bitmap),
 * and stores in `count`. This length will be rounded down to times of 8.
 */
int init_alloc_table(struct alloc_table *table, void *area, int area_size, int elem_size, int *count)
{
    return -1;
}

int destroy_alloc_table(struct alloc_table *table)
{
    return -1;
}

int test_bit(struct alloc_table *table, int index)
{
    // TODO!
    register int res asm("ax");
    asm volatile(
        "bt %2, %3\n"
        "setb %%al"
        : "=a"(res)
        : "0"(0), "r"(index), "m"(table->pmem.bitmap)
    );
    return res;
}
