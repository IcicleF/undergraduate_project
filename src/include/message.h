#if !defined(MESSAGE_H)
#define MESSAGE_H

#include "common.h"

#define __PACKED__ __attribute__((packed))

#define RPC_ALLOC       0x00000001
#define RPC_DEALLOC     0x00000002

struct message
{
    uint32_t magic;

    uint32_t type;
    uint32_t src;
    uint32_t dst;

    union
    {
        uint8_t raw_data[52];
    
        struct
        {
            uint32_t count;
        } alloc;

        struct
        {
            uint32_t success;
            uint64_t addr;
        } __PACKED__ alloc_response;
    } __PACKED__;
} __PACKED__;

_Static_assert(sizeof(struct message) == MESSAGE_LEN, "MESSAGE_LEN != sizeof(struct message)");

#endif // MESSAGE_H
