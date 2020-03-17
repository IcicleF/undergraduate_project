#include <rpc.hpp>
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

/* Stops RPCInterface listener threads and the underlying RDMASocket. */
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

void RPCInterface::rpcListen()
{
    
}
