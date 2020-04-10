/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(RPC_HPP)
#define RPC_HPP

#include <boost/lockfree/spsc_queue.hpp>
#include "rdma.hpp"

/** Buffers an incoming RPC request/response. */
struct ReqBuf
{
    ibv_wc wc;
    Message msg;
    explicit ReqBuf() = default;
    ReqBuf(const ibv_wc &wc, Message *msg) : wc(wc), msg(*msg) { }
};

/**
 * Handles all RPC requests, both in and out.
 * One node should only have only one RPC interface.
 */
class RPCInterface
{
public:
    explicit RPCInterface();
    ~RPCInterface();

    inline void __markAsAlive(int peerId) { peerAliveStatus[peerId] = 1; }
    inline void __markAsDead(int peerId) { peerAliveStatus[peerId] = -1; }
    inline void __cancelMarking(int peerId) { peerAliveStatus[peerId] = 0; }

    bool isPeerAlive(int peerId);
    void stopListenerAndJoin();
    void syncAmongPeers();

    inline void remoteReadFrom(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length, uint32_t taskId = 0)
    {
        socket->postRead(peerId, remoteSrcShift, localDst, length, taskId);
    }
    inline void remoteWriteTo(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm = -1)
    {
        socket->postWrite(peerId, remoteDstShift, localSrc, length, imm);
    }
    
    void registerRPCProcessor(void (*rpcProc)(const RPCMessage *, RPCMessage *));
    void rpcCall(int peerId, const Message *request, Message *response);

    inline RDMASocket *getRDMASocket() const { return socket; }

private:
    using Queue = boost::lockfree::spsc_queue<ReqBuf, boost::lockfree::capacity<65536>>;

    void rdmaListen();
    void rpcListen();

    RDMASocket *socket = nullptr;
    std::thread rdmaListener;           /* Poll filtered recvs and distribute them */
    std::thread rpcListener;            /* Poll distributed CQEs */
    void (*rpcProcessor)(const RPCMessage *request, RPCMessage *response) = nullptr;

    short peerAliveStatus[MAX_NODES];
    std::atomic<bool> shouldRun;
    Queue sq;                           /* Responses of sent (outbound) RPCs */
    Queue rq;                           /* Received (inbound) RPC requests */
    Queue oq;                           /* Other types of message */
};

#endif // RPC_HPP
