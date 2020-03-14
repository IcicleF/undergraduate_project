#if !defined(MESSAGE_HPP)
#define MESSAGE_HPP

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "commons.hpp" 

struct RPCMessage
{
    enum {
        RPC_UNDEF = 0,
        RPC_REQUEST,
        RPC_RESPONSE,
        RPC_INVALID
    } type;
};

struct Message
{
    enum {
        MESG_UNDEF = 0,
        MESG_REMOTE_MR,
        MESG_RPC_CALL,
        MESG_RPC_RESPONSE,
        MESG_SYNC_REQUEST,
        MESG_SYNC_RESPONSE,
        MESG_INVALID
    } type;

    union {
        ibv_mr mr;
        RPCMessage rpc;
    } data;
};

#endif // MESSAGE_HPP
