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
    SP_REMOTE_WRLOG_READ,
    SP_SYNC_RECV,
    SP_TYPES
};

struct Message
{
    enum {
        MESG_UNDEF = 0,
        MESG_REMOTE_MR,
        MESG_RECOVER_START,
        MESG_RECOVER_RESPONSE,
        MESG_RECOVER_END,
        MESG_INVALID
    } type;

    union {
        struct 
        {
            ibv_mr mr;
            int size;
        };
    } data;
};

#endif // MESSAGE_HPP