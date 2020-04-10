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

    shouldRun.store(true);
    rdmaListener = std::thread(&RPCInterface::rdmaListen, this);
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
 * Stops incoming-RPC listener and joins the listener thread.
 * @note Outbounds RPCs and raw RDMA services are not disabled.
 */
void RPCInterface::stopListenerAndJoin()
{
    if (std::this_thread::get_id() != mainThreadId) {
        d_err("cannot execute stopListenerAndJoin from non-main threads");
        return;
    }
    
    d_info("first, try to stop RDMASocket...");
    socket->stopListenerAndJoin();

    shouldRun = false;
    if (rpcListener.joinable())
        rpcListener.join();
}

void RPCInterface::registerRPCProcessor(void (*rpcProc)(const RPCMessage *, RPCMessage *))
{
    rpcProcessor = rpcProc;
}

/**
 * Poll RDMA recv's (already filtered by RDMASocket::pollRecvCompletion), and distribute them
 * to different SPSC queues.
 * This mechanism is designed to allow a node to act as both RPC Server and RPC Client.
 * @note This function should be ran as a new thread.
 */
void RPCInterface::rdmaListen()
{
    ibv_wc wc[2];

    while (shouldRun.load()) {
        int ret = socket->pollRecvCompletion(wc);
        if (!ret) {
            d_info("pollRecvCompletion returned 0, indicating RDMASocket has stopped.");
            break;
        }
        int peerId = WRID_PEER(wc->wr_id);
        auto *msg = reinterpret_cast<Message *>(socket->getRecvRegion(peerId));

        /* Here comes extra data copies. TODO: Consider the tradeoffs. */
        ReqBuf rbuf(wc[0], msg);
        if (msg->type == Message::MESG_RPC_CALL)
            while (!rq.push(rbuf));
        else if (msg->type == Message::MESG_RPC_RESPONSE)
            while (!sq.push(rbuf));
        else
            while (!oq.push(rbuf));
        
        socket->postReceive(peerId, sizeof(Message));
    }
    
    d_info("rdmaListen has safely exited.");
}

/** 
 * Listen for incoming RPC requests.
 * @note This function should be ran as a new thread.
 */
void RPCInterface::rpcListen()
{
    ReqBuf rbuf;
    Message response;
    response.type = Message::MESG_RPC_RESPONSE;
    while (true) {
        while (shouldRun.load() && !rq.pop(rbuf));
        if (!shouldRun.load())
            break;

        int peerId = WRID_PEER(rbuf.wc.wr_id);
        if (rpcProcessor)
            rpcProcessor(&rbuf.msg.data.rpc, &response.data.rpc);
        memcpy(socket->getSendRegion(peerId), &response, sizeof(Message));
        socket->postSend(peerId, sizeof(Message));
    }

    d_info("rpcListen has safely exited.");
}

/** Invoke an RPC. */
void RPCInterface::rpcCall(int peerId, const Message *request, Message *response)
{
    //d_info("RPC call to %d, type %d", peerId, (int)request->data.rpc.type);
    memcpy(socket->getSendRegion(peerId), request, sizeof(Message));
    socket->postSend(peerId, sizeof(Message));

    ReqBuf rbuf;
    if (request->type == Message::MESG_RPC_CALL)
        while (shouldRun.load() && !sq.pop(rbuf));      /* Normal RPC call, expect RPC response */
    else
        while (shouldRun.load() && !oq.pop(rbuf));      /* Special RPC call, expect special response */
    expectTrue(WRID_PEER(rbuf.wc.wr_id) == peerId);
    memcpy(response, &rbuf.msg, sizeof(Message));
}
