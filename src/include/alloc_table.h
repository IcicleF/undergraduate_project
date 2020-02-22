#if !defined(ALLOC_TABLE_H)
#define ALLOC_TABLE_H

#include "common.h"

/* A block list. May be replaced by slab/AVL/etc. later */
struct block_list
{
    int start_id;           /* Start block of this segment */
    int length;             /* Segment length */

    struct block_list *prev;
    struct block_list *next;
};

struct alloc_table
{
    struct
    {
        uint64_t *alloc_table_magic;
        uint8_t *bitmap;
    } pmem;
    
    int length;                         /* Bitmap length in bits (equivalent to # of blocks) */
    struct block_list *alloc_head;      /* Allocated block list */
    struct block_list *free_head;       /* Free list */
};

#endif // ALLOC_TABLE_H
