#include <network/rpc.hpp>
#include <debug.hpp>

/*
 * Constructor initializes `clusterConf` and `myNodeConf`.
 */
RPCInterface::RPCInterface()
{
    memset(peerAliveStatus, 0, sizeof(peerAliveStatus));
    rdma = new RDMASocket();
    shouldRun = true;
}

/* Destructor deallocates `clusterConf` and `myNodeConf`. */
RPCInterface::~RPCInterface()
{
    delete rdma;
}

/*
 * Retrieve peer liveness data from RDMA socket.
 * Users can "regard" a peer as alive/dead by __markAsAlive and __markAsDead,
 * and cancel this by __cancelMarking. 
 */
bool RPCInterface::isPeerAlive(int peerId)
{
    if (short res = peerAliveStatus[peerId])
        return res > 0;
    return rdma->isPeerAlive(peerId);
}

/** 
 * Listen for incoming RPC requests.
 * @note This function should be ran as a new thread.
 */
void RPCInterface::rpcListen(int rounds)
{
    ibv_wc wc[2];
    int respSize;
    for (int i = 0; i < rounds; ++i)
        if (rdma->tryPollRecvCompletion(wc) > 0) {
            int peerId = WRID_PEER(wc->wr_id);
            auto *req = rdma->getRecvRegion(peerId);
            auto *resp = rdma->getSendRegion(peerId);

            int *typeRegion = reinterpret_cast<int *>(req);
            RpcType reqType = static_cast<RpcType>(*typeRegion);
            rpcProcessor(reqType, req + sizeof(int), resp + sizeof(int), &respSize);

            typeRegion = reinterpret_cast<int *>(resp);
            *typeRegion = static_cast<int>(RpcType::RPC_RESPONSE);
            rdma->postSend(peerId, respSize + sizeof(int));
        }
}

/** Invoke an RPC. */
void RPCInterface::rpcCall(int peerId, RpcType reqType, const void *request, size_t reqSize,
                           void *response, size_t respSize)
{
    auto *buf = rdma->getSendRegion(peerId);
    int *typeRegion = reinterpret_cast<int *>(buf);
    *typeRegion = static_cast<int>(reqType);

    memcpy(buf + sizeof(int), request, reqSize);
    rdma->postSend(peerId, reqSize + sizeof(int));
    
    ibv_wc wc[2];
    while (rdma->tryPollRecvCompletion(wc) == 0);

    buf = reinterpret_cast<uint8_t *>(rdma->getRecvRegion(peerId));
    typeRegion = reinterpret_cast<int *>(buf);
    if (*typeRegion != static_cast<int>(RpcType::RPC_RESPONSE))
        d_warn("response is not RPC_RESPONSE");
    memcpy(response, buf + sizeof(int), respSize);
}
