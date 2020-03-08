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
    using BlockTy = Block1K;

public:
    explicit RPCInterface();
    ~RPCInterface();

    void __markAsAlive(int peerId) { peerIsAlive[peerId] = true; }
    void __markAsDead(int peerId) { peerIsAlive[peerId] = false; }
    bool isAlive(int peerId) { return peerIsAlive[peerId]; }

    int remoteReadFrom(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length)
    {
        if (socket)
            return socket->postRead(peerId, remoteSrcShift, localDst, length);
        return -1;
    }
    int remoteWriteTo(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm = -1)
    {
        if (socket)
            return socket->postWrite(peerId, remoteDstShift, localSrc, length, imm);
        return -1;
    }
    
    void rpcListen();
    int rpcProcessCall(int peerId, const RPCMessage *message, RPCMessage *response);
    int remoteRPCCall(int peerId, const RPCMessage *request, RPCMessage *response);

    __always_inline RDMASocket *getRDMASocket() const { return socket; }
    __always_inline void registerAllocTable(AllocationTable<BlockTy> *allocTable)
    {
        this->allocTable = allocTable;
    }

private:
    RDMASocket *socket = nullptr;
    std::atomic<uint64_t> taskId;
    std::thread rpcListener;

    AllocationTable<BlockTy> *allocTable;
    std::unordered_map<int, bool> peerIsAlive;

    uint64_t remoteAllocBlock(int peerId);
    int remoteDeallocBlock(int peerId, uint64_t addr);
};

#endif // RPC_HPP
