#include <stdio.h>
#include <stdlib.h>

#include <alloctable.h>
#include <debug.h>

/**
 * Initialize the allocation table (ALT).
 * 
 * Memory Organization:
 * 
 * +---- mem_loc
 * |
 * V
 * +-------+----------+--------------------------------+
 * | Magic |  Bitmap  |          Element pool          |
 * +-------+----------+--------------------------------+
 * \                                                  /
 *  \__________________ mem_size ____________________/
 * 
 * This function automacally calculates the element pool's length (= #bits in Bitmap),
 * This length will be rounded down to times of 64.
 * 
 * Persistent memory configuration structure `conf` should HAVE BEEN initialized.
 */
int init_alloc_table(struct alloc_table *table, struct mem_config *conf, int elem_size)
{
    int tmp, bitmap_bytes;
    void *area = conf->mem_loc;
    int area_size = conf->mem_size;

    if (((uint64_t)area) % 8 != 0) {
        d_err("memory location should be divisible by 8");
        return -1;
    }
    if (elem_size % 8 != 0) {
        elem_size = (elem_size & (-8)) + 8;
        d_warn("elem_size should be divisible by 8, rounded up to %d", elem_size);
    }

    /* Go read the initialized allocation table */
    if (*(table->pmem.alloc_table_magic) == ALLOC_TABLE_MAGIC) {
        d_info("magic indicates that the table has been initialized");
        return read_alloc_table(table, conf, elem_size);
    }

    table->pmem.alloc_table_magic = area;
    table->pmem.bitmap = area + 8;                          /* 4-byte padding */
    
    tmp = (area_size - 8) / (elem_size * 64 + 8);           /* packaged 64 * (elem + 1b) pairs */
    bitmap_bytes = tmp * 8;

    table->pmem.mapped_area = table->pmem.bitmap + bitmap_bytes;
    table->elem_size = elem_size;
    table->length = bitmap_bytes * 8;
    table->alloced = 0;
    memset(table->pmem.bitmap, -1, bitmap_bytes);           /* Initialize to all-1 */

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
    struct block_list *head, *ptr;

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

int test_bit(struct alloc_table *table, long index)
{
    register int ret asm("eax");
    asm volatile(
        "btq %2, %3;"
        "setb %%al"
        : "=r"(ret)
        : "0"(0), "r"(index), "m"(*(table->pmem.bitmap))
        : "cc"
    );
    return ret;
}

int set_bit(struct alloc_table *table, long index)
{
    register int ret asm("eax");
    asm volatile(
        "lock btsq %2, %3;"
        "setb %%al"
        : "=r"(ret)
        : "0"(0), "r"(index), "m"(*(table->pmem.bitmap))
        : "cc"
    );
    return ret;
}

int clear_bit(struct alloc_table *table, long index)
{
    register int ret asm("eax");
    asm volatile(
        "lock btrq %2, %3;"
        "setb %%al"
        : "=r"(ret)
        : "0"(0), "r"(index), "m"(*(table->pmem.bitmap))
        : "cc"
    );
    return ret;
}

long find_zero_bit(struct alloc_table *table, long start_from)
{
    register long ret asm("rax");

    start_from &= -8;
    if (start_from < 0 || start_from >= table->length)
        return -1;

    asm volatile(
        "bsf %1, %0"
        : "=r"(ret)
        : "m"(*(table->pmem.bitmap + start_from / 8))
    );
    ret += start_from;

    if (ret >= table->length)
        return -1;
    return ret;
}

void *alloc_elem(struct alloc_table *table, long *index)
{
    long ret = 0;

    while (1) {
        ret = find_zero_bit(table, ret);
        if (unlikely(ret == -1))
            return NULL;
        if (likely(clear_bit(table, ret) == 1))     /* If successfully cleared the 1, returns */
            break;
    }

    table->alloced++;
    *index = ret;
    return ELEM_AT(table, ret);
}

void free_elem(struct alloc_table *table, void *elem)
{
    uint64_t offset = (uint64_t)elem - (uint64_t)(table->pmem.mapped_area);
    long index = offset / table->elem_size;

    if (unlikely(offset % table->elem_size != 0)) {
        d_err("elem is not well-aligned");
        return;
    }
    if (unlikely(set_bit(table, index) == 1)) {
        d_err("bit #%ld is already set", index);
        return;
    }
    
    table->alloced--;
}
