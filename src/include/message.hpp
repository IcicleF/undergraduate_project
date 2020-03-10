#if !defined(MESSAGE_HPP)
#define MESSAGE_HPP

#include "commons.hpp"

#define RPC_ALLOC               0x00000001
#define RPC_DEALLOC             0x00000002
#define RPC_HEARTBEAT           0x10000001
#define RPC_DISCONNECT          0x10000002
#define RPC_TEST                0x70000001
#define RPC_NOT_IMPL            0x77777777

#define RPC_RESPONSE            0x80000000
#define RPC_RESPONSE_ACK        0x80000001
#define RPC_RESPONSE_NAK        0x80000002
#define RPC_FAILED              0xF0000000

union RPCMessage
{
    uint8_t rawData[32];

    struct
    {
        uint64_t type;
        uint64_t uid;
        uint64_t count;
        uint64_t addr;
    };
};

#endif // MESSAGE_HPP
