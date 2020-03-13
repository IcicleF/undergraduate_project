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

    void __markAsAlive(int peerId) { peerIsAlive[peerId] = 1; }
    void __markAsDead(int peerId) { peerIsAlive[peerId] = -1; }
    void __markAsNatural(int peerId) { peerIsAlive[peerId] = 0; }
    bool isAlive(int peerId)
    {
        if (short res = peerIsAlive[peerId])
            return res > 0;
        return socket->isPeerAlive(peerId);
    }

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
    int rpcProcessCall(int peerId, const Message *message, Message *response);
    int remoteRPCCall(int peerId, const Message *request, Message *response);

    __always_inline RDMASocket *getRDMASocket() const { return socket; }

private:
    RDMASocket *socket = nullptr;
    HashTable *hashTable = nullptr;

    std::unordered_map<int, short> peerIsAlive;
};

#endif // RPC_HPP
