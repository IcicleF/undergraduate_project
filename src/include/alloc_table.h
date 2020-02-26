#if !defined(ALLOC_TABLE_H)
#define ALLOC_TABLE_H

#include <stdlib.h>
#include <pthread.h>
#include <infiniband/verbs.h>
#include <libpmem.h>
#include <libpmemobj.h>

#include "common.h"
#include "config.h"

/* A block list. May be replaced by slab/AVL/etc. later */
struct block_list
{
    int start_id;           /* Start block of this segment */
    int length;             /* Segment length */

    struct block_list *prev;
    struct block_list *next;
};

/* Inode/block allocation table */
struct alloc_table
{
    struct
    {
        uint32_t *alloc_table_magic;    /* Detect if the pmem has been initialized */
        uint8_t *bitmap;                /* Bitmap array start location */
        void *mapped_area;              /* Mapped area start location */
    } pmem;

    pthread_mutex_t mutex;
    
    int elem_size;                      /* Element size */
    long length;                        /* Bitmap length in bits (equivalent to # of blocks) */
    struct block_list *alloc_head;      /* Allocated block list */
    struct block_list *free_head;       /* Free list */
};

int init_alloc_table(struct alloc_table *table, struct mem_config *conf, int elem_size);
int read_alloc_table(struct alloc_table *table, struct mem_config *conf, int elem_size);
int destroy_alloc_table(struct alloc_table *table);

int test_bit(struct alloc_table *table, long index);
int set_bit(struct alloc_table *table, long index);
int clear_bit(struct alloc_table *table, long index);
long find_zero_bit(struct alloc_table *table, long start_from);

void *alloc_elem(struct alloc_table *table);


#endif // ALLOC_TABLE_H
