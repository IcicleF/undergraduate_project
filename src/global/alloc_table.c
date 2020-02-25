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
 * This length will be rounded down to times of 8.
 * 
 * Persistent memory configuration structure `conf` should HAVE BEEN initialized.
 */
int init_alloc_table(struct alloc_table *table, struct mem_config *conf, int elem_size)
{
    int bitmap_bytes;
    void *area = conf->mem_loc;
    int area_size = conf->mem_size;

    if (*(table->pmem.alloc_table_magic) == ALLOC_TABLE_MAGIC) {
        d_info("magic indicates that the table has been initialized");
        return read_alloc_table(table, conf, elem_size);
    }
    if (elem_size % 8 != 0) {
        elem_size = (elem_size & (-8)) + 8;
        d_warn("elem_size should be divisible by 8, rounded up to %d", elem_size);
    }

    table->pmem.alloc_table_magic = area;
    table->pmem.bitmap = area + 8;                          /* 4-byte padding */
    
    bitmap_bytes = (area_size - 8) / (elem_size * 8 + 1);   /* packaged 8 * (elem + 1b) pairs */

    table->pmem.mapped_area = table->pmem.bitmap + bitmap_bytes;
    table->elem_size = elem_size;
    table->length = bitmap_bytes * 8;

    memset(table->pmem.bitmap, 0, bitmap_bytes);

    table->alloc_head = NULL;
    table->free_head = malloc(sizeof(struct block_list));   /* initialize in-DRAM free list */
    memset(table->free_head, 0, sizeof(struct block_list));
    table->free_head->start_id = 0;
    table->free_head->length = table->length;

    *(table->pmem.alloc_table_magic) = ALLOC_TABLE_MAGIC;
    mem_force_flush(table->pmem.alloc_table_magic);         /* persist */

    return 0;
}

/* Free in-DRAM block lists */
int destroy_alloc_table(struct alloc_table *table)
{
    struct block_list *head;
    struct block_list *ptr;

    head = table->alloc_head;
    table->alloc_head = NULL;
    while (head != NULL) {
        ptr = head->next;
        free(head);
        head = ptr;
    }

    head = table->free_head;
    table->free_head = NULL;
    while (head != NULL) {
        ptr = head->next;
        free(head);
        head = ptr;
    }

    return 0;
}

int test_bit(struct alloc_table *table, int index)
{
    register int res asm("eax");
    asm volatile(
        "bt %2, %3;"
        "setb %%al"
        : "=a"(res)
        : "0"(0), "r"(index), "m"(*(table->pmem.bitmap))
        : "cc"
    );
    return res;
}

int set_bit(struct alloc_table *table, int index)
{
    register int res asm("eax");
    asm volatile(
        "lock bts %2, %3;"
        "setb %%al"
        : "=a"(res)
        : "0"(0), "r"(index), "m"(*(table->pmem.bitmap))
        : "cc"
    );
    return res;
}

int clear_bit(struct alloc_table *table, int index)
{
    register int res asm("eax");
    asm volatile(
        "lock btr %2, %3;"
        "setb %%al"
        : "=a"(res)
        : "0"(0), "r"(index), "m"(*(table->pmem.bitmap))
        : "cc"
    );
    return res;
}


