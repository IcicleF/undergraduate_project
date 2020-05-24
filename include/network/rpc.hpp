/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(RPC_HPP)
#define RPC_HPP

#include "rdma.hpp"

enum class RpcType
{
    RPC_REQUEST = 64,
    RPC_TEST,
    RPC_MEMREAD,
    RPC_MEMWRITE,
    RPC_OPEN,
    RPC_ACCESS,
    RPC_CREATE,
    RPC_CSIZE,
    RPC_READ,
    RPC_WRITE,
    RPC_REMOVE,
    RPC_FILESTAT,
    RPC_DIRSTAT,
    RPC_MKDIR,
    RPC_RMDIR,
    RPC_READDIR,
    RPC_RESPONSE = 128
};

/**
 * Handles all RPC requests, both in and out.
 * One node should only have only one RPC interface.
 */
class RPCInterface
{
private:
    void (*rpcProcessor)(RpcType reqType, const void *request, void *response, int *respSize);

public:
    explicit RPCInterface();
    ~RPCInterface();

    inline void __markAsAlive(int peerId) { peerAliveStatus[peerId] = 1; }
    inline void __markAsDead(int peerId) { peerAliveStatus[peerId] = -1; }
    inline void __cancelMarking(int peerId) { peerAliveStatus[peerId] = 0; }

    bool isPeerAlive(int peerId);

    void registerRPCProcessor(decltype(rpcProcessor) rpcProc) { rpcProcessor = rpcProc; }
    void rpcCall(int peerId, RpcType reqType, const void *request, size_t reqSize,
                 void *response, size_t respSize);
    void rpcListen(int rounds = 1000000);
    void server() { while (shouldRun) rpcListen(); }

    inline RDMASocket *getRDMASocket() const { return rdma; }
    inline void stop() { shouldRun = false; }

private:
    RDMASocket *rdma = nullptr;

    short peerAliveStatus[MAX_NODES];
    bool shouldRun;
};

#endif // RPC_HPP
