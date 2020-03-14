#if !defined(RPC_HPP)
#define RPC_HPP

#include "config.hpp"
#include "rdma.hpp"
#include "message.hpp"
#include "alloctable.hpp"

/*
 * Handles all RPC requests, both in and out.
 * One node should only have only one RPC interface.
 */
class RPCInterface
{
public:
    using BlockTy = Block2K;

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
    uint32_t remoteReadFrom(int peerId, uint64_t remoteSrcShift, uint64_t localDst,
                            uint64_t length, int specialTaskId = -1)
    {
        return socket->postRead(peerId, remoteSrcShift, localDst, length, specialTaskId);
    }
    __always_inline
    uint32_t remoteWriteTo(int peerId, uint64_t remoteDstShift, uint64_t localSrc,
                           uint64_t length, int imm = -1, int specialTaskId = -1)
    {
        return socket->postWrite(peerId, remoteDstShift, localSrc, length, imm, specialTaskId);
    }
    
    void rpcListen();
    int rpcProcessCall(int peerId, const Message *message, Message *response);
    int remoteRPCCall(int peerId, const Message *request, Message *response);

    __always_inline HashTable *getHashTable() { return hashTable; }
    __always_inline RDMASocket *getRDMASocket() const { return socket; }

private:
    RDMASocket *socket = nullptr;
    std::thread rpcListener;

    HashTable *hashTable = nullptr;
    short peerAliveStatus[MAX_NODES];

    bool shouldRun;
};

#endif // RPC_HPP
