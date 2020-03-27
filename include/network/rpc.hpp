/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(RPC_HPP)
#define RPC_HPP

#include "rdma.hpp"

/**
 * Handles all RPC requests, both in and out.
 * One node should only have only one RPC interface.
 */
class RPCInterface
{
public:
    explicit RPCInterface();
    ~RPCInterface();

    __always_inline void __markAsAlive(int peerId) { peerAliveStatus[peerId] = 1; }
    __always_inline void __markAsDead(int peerId) { peerAliveStatus[peerId] = -1; }
    __always_inline void __cancelMarking(int peerId) { peerAliveStatus[peerId] = 0; }

    bool isPeerAlive(int peerId);
    void stopListenerAndJoin();
    void syncAmongPeers();

    __always_inline
    void remoteReadFrom(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length, uint32_t taskId = 0)
    {
        socket->postRead(peerId, remoteSrcShift, localDst, length, taskId);
    }
    __always_inline
    void remoteWriteTo(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm = -1)
    {
        socket->postWrite(peerId, remoteDstShift, localSrc, length, imm);
    }
    
    void registerRPCProcessor(void (*rpcProc)(const RPCMessage *, RPCMessage *));
    void rpcListen();
    void rpcCall(int peerId, const Message *request, Message *response);

    __always_inline RDMASocket *getRDMASocket() const { return socket; }

private:
    RDMASocket *socket = nullptr;
    std::thread rpcListener;
    void (*rpcProcessor)(const RPCMessage *request, RPCMessage *response) = nullptr;

    short peerAliveStatus[MAX_NODES];
    bool shouldRun;
};

#endif // RPC_HPP
