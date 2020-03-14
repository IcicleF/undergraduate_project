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
    hashTable = new HashTable();
    socket->registerHashTable(hashTable);

    shouldRun = true;
    rpcListener = std::thread(&RPCInterface::rpcListen, this);
}

/* Destructor deallocates `clusterConf` and `myNodeConf`. */
RPCInterface::~RPCInterface()
{
    if (cmdConf) {
        delete cmdConf;
        cmdConf = nullptr;
    }
    if (myNodeConf) {
        delete myNodeConf;
        myNodeConf = nullptr;
    }

    delete socket;
    delete hashTable;
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

/* Stops RPCInterface listener threads and the underlying RDMASocket. */
void RPCInterface::stopAndJoin()
{
    if (std::this_thread::get_id() != mainThreadId) {
        d_err("cannot stopAndJoin from non-main threads");
        return;
    }
    if (!shouldRun)
        return;
    
    shouldRun = false;
    if (rpcListener.joinable())
        rpcListener.join();
    
    d_info("all joinable listener threads have joined");
    d_info("now, try to stop RDMASocket...");
    socket->stopAndJoin();
}

void RPCInterface::rpcListen()
{
    
}
