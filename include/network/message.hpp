/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(MESSAGE_HPP)
#define MESSAGE_HPP

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "../commons.hpp" 

enum SpecialWRType
{
    SP_REMOTE_MR_RECV = 1,
    SP_SYNC_RECV,
    SP_TYPES
};

struct RPCMessage
{
    enum {
        RPC_UNDEF = 0,
        RPC_OPEN,
        RPC_ACCESS,
        RPC_CREATE,
        //RPC_RENAME,       // RENAME is currently to-do
        RPC_CSIZE,
        RPC_REMOVE,
        RPC_STAT,
        RPC_MKDIR,
        RPC_RMDIR,
        RPC_OPENDIR,
        RPC_READDIR,
    } type;

    int result;
    union
    {
        int mode;
        int flags;
    };
    union
    {
        struct
        {
            char path[256];
            uint8_t raw2[256];
        };
        uint8_t raw[512];
        uint64_t raw64[64];
    };
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

static_assert(sizeof(Message) < RDMA_BUF_SIZE, "Message too large");

#endif // MESSAGE_HPP
