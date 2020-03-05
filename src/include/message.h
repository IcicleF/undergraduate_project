#if !defined(MESSAGE_H)
#define MESSAGE_H

#include "common.h"

#define MSG_FAILED              0xFECBADEF
#define MSG_ALLOC               0x00000001
#define MSG_ALLOC_RESPONSE      (~RPC_ALLOC)
#define MSG_DEALLOC             0x00000002
#define MSG_DEALLOS_RESPONSE    (~RPC_DEALLOC)
#define MSG_HEARTBEAT           0x10000001
#define MSG_DISCONNECT          0x10000002
#define MSG_NOT_IMPL            0x88888888

struct message
{
    uint64_t uid;
    uint32_t type;

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
