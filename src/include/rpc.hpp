#if !defined(RPC_HPP)
#define RPC_HPP

#include "config.hpp"
#include "rdma.hpp"
#include "message.hpp"

/*
 * Handles all RPC requests, both in and out.
 * One node should only have only one RPC interface.
 */
class RPCInterface
{
public:
    explicit RPCInterface();
    ~RPCInterface();

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
    int remoteAllocBlock(int peerId);
    int remoteDeallocBlock(int peerId);

    __always_inline RDMASocket *getRDMASocket() const { return socket; }

private:
    RDMASocket *socket = nullptr;
    std::atomic<uint64_t> taskId;
    std::thread rpcListener;

    void rpcListen();
    int rpcProcessCall(int peerId, const RPCMessage *message, RPCMessage *response);
    int remoteRPCCall(int peerId, const RPCMessage *request, RPCMessage *response);
};

#endif // RPC_HPP
