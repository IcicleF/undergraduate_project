#if !defined(ALLOC_TABLE_H)
#define ALLOC_TABLE_H

#include <stdlib.h>
#include <pthread.h>
#include <infiniband/verbs.h>

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
        void *bitmap;                   /* Bitmap array start location */
        void *mapped_area;              /* Mapped area start location */
    } pmem;

    pthread_mutex_t mutex;
    
    int elem_size;                      /* Element size */
    int length;                         /* Bitmap length in bits (equivalent to # of blocks) */
    struct block_list *alloc_head;      /* Allocated block list */
    struct block_list *free_head;       /* Free list */
};

int init_alloc_table(struct alloc_table *table, void *area, int area_size, int elem_size, int *count);
int destroy_alloc_table(struct alloc_table *table);

int test_bit(struct alloc_table *table, int index);
int set_bit(struct alloc_table *table, int index);
int clear_bit(struct alloc_table *table, int index);

#endif // ALLOC_TABLE_H
