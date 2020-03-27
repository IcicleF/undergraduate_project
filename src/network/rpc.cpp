#include <network/rpc.hpp>
#include <debug.hpp>

/*
 * Constructor initializes `clusterConf` and `myNodeConf`.
 */
RPCInterface::RPCInterface()
{
    if (cmdConf == nullptr || memConf == nullptr) {
        d_err("cmdConf & memConf should be initialized!");
        exit(-1);
    }

    if (clusterConf != nullptr || myNodeConf != nullptr)
        d_warn("clusterConf & myNodeConf were already initialized, skip");
    else {
        clusterConf = new ClusterConfig(cmdConf->clusterConfigFile);
        
        auto myself = clusterConf->findMyself();
        if (myself.id >= 0)
            myNodeConf = new NodeConfig(myself);
        else {
            d_err("cannot find configuration of this node");
            exit(-1);
        }
    }

    memset(peerAliveStatus, 0, sizeof(peerAliveStatus));
    socket = new RDMASocket();

    shouldRun = true;
    rpcListener = std::thread(&RPCInterface::rpcListen, this);
}

/* Destructor deallocates `clusterConf` and `myNodeConf`. */
RPCInterface::~RPCInterface()
{
    shouldRun = false;
    stopListenerAndJoin();

    if (cmdConf) {
        delete cmdConf;
        cmdConf = nullptr;
    }
    if (myNodeConf) {
        delete myNodeConf;
        myNodeConf = nullptr;
    }

    delete socket;
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
    return socket->isPeerAlive(peerId);
}

/*
 * Synchronize a special RPC message among all peers.
 * - Send to: all peers with id < myId
 * - Recv from: all peers with id > myId
 * 
 * This function blocks until all messages are responded/received.
 */
void RPCInterface::syncAmongPeers()
{
    ibv_wc wc[2];

    for (int i = 0; i < myNodeConf->id; ++i) {
        auto *msg = reinterpret_cast<Message *>(socket->peers[i].sendRegion);
        msg->type = Message::MESG_SYNC_REQUEST;
        socket->postSend(i, sizeof(Message));

        socket->postReceive(i, sizeof(Message), SP_SYNC_RECV);
        expectPositive(socket->pollRecvCompletion(wc));
        expectTrue(WRID_PEER(wc->wr_id) == i && WRID_TASK(wc->wr_id) == SP_SYNC_RECV);
        auto *resp = reinterpret_cast<Message *>(socket->peers[i].recvRegion);
        expectTrue(resp->type == Message::MESG_SYNC_RESPONSE);
    }

    for (int i = myNodeConf->id + 1; i < clusterConf->getClusterSize(); ++i) {
        socket->postReceive(i, sizeof(Message), SP_SYNC_RECV);
        expectPositive(socket->pollRecvCompletion(wc));
        expectTrue(WRID_PEER(wc->wr_id) == i && WRID_TASK(wc->wr_id) == SP_SYNC_RECV);
        auto *msg = reinterpret_cast<Message *>(socket->peers[i].recvRegion);
        expectTrue(msg->type == Message::MESG_SYNC_REQUEST);

        auto *resp = reinterpret_cast<Message *>(socket->peers[i].sendRegion);
        resp->type = Message::MESG_SYNC_RESPONSE;
        socket->postSend(i, sizeof(Message));
    }
}

/**
 * Stops RPCInterface listener threads and the underlying RDMASocket.
 * This function should be called by ALL nodes after they ensure that everything has finished.
 */
void RPCInterface::stopListenerAndJoin()
{
    if (std::this_thread::get_id() != mainThreadId) {
        d_err("cannot execute stopListenerAndJoin from non-main threads");
        return;
    }
    if (!shouldRun)
        return;
    
    shouldRun = false;
    if (rpcListener.joinable())
        rpcListener.join();
    
    d_info("all joinable listener threads have joined");
    d_info("now, try to stop RDMASocket...");
    socket->stopListenerAndJoin();
}

void RPCInterface::registerRPCProcessor(void (*rpcProc)(const RPCMessage *, RPCMessage *))
{
    rpcProcessor = rpcProc;
}

/** 
 * Listen for incoming RPC requests.
 * @note This function should run as a new thread.
 */
void RPCInterface::rpcListen()
{
    /* Initial RDMA recv */
    if (shouldRun)
        for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
            int peerId = (*clusterConf)[i].id;
            socket->postReceive(peerId, RDMA_BUF_SIZE, peerId);
        }

    ibv_wc wc[2];
    RPCMessage request;
    Message response;
    response.type = Message::MESG_RPC_RESPONSE;
    while (shouldRun) {
        expectPositive(socket->pollRecvCompletion(wc));
        int peerId = WRID_PEER(wc->wr_id);
        auto *msg = reinterpret_cast<Message *>(socket->getRecvRegion(peerId));

        if (msg->type == Message::MESG_RPC_CALL) {
            auto const *rpcMsg = &msg->data.rpc;
            memcpy(&request, rpcMsg, sizeof(RPCMessage));
            socket->postReceive(peerId, RDMA_BUF_SIZE, peerId);

            if (rpcProcessor)
                rpcProcessor(&request, &response.data.rpc);
            memcpy(socket->getSendRegion(peerId), &response, sizeof(Message));
            socket->postSend(peerId, sizeof(Message));
        }
        else 
            d_warn("unexpected message type %d caught by rpcListen", (int)msg->type);
    }

    d_info("RPC listener safely exited");
}

/** Invoke an RPC. */
void RPCInterface::rpcCall(int peerId, const Message *request, Message *response)
{
    memcpy(socket->getSendRegion(peerId), request, sizeof(Message));
    socket->postSend(peerId, sizeof(Message));
    socket->postReceive(peerId, sizeof(Message), peerId);
    
    ibv_wc wc[2];
    expectPositive(socket->pollRecvCompletion(wc));
    memcpy(response, socket->getRecvRegion(peerId), sizeof(Message));
    if (response->type != Message::MESG_RPC_RESPONSE)
        d_err("unexpected response type: %d", (int)response->type);
}
