#include <rpc.hpp>
#include <debug.hpp>

/*
 * Constructor initializes `clusterConf` and `myNodeConf`.
 */
RPCInterface::RPCInterface() : taskId(1)
{
    if (cmdConf == nullptr || memConf == nullptr) {
        d_err("cmdConf & memConf should be initialized!");
        exit(-1);
    }
    if (clusterConf != nullptr || myNodeConf != nullptr)
        d_warn("clusterConf & myNodeConf are already initialized, skip");
    else {
        clusterConf = new ClusterConfig(cmdConf->clusterConfigFile);
        
        auto _myself = clusterConf->findMyself();
        if (_myself.has_value())
            myNodeConf = new NodeConfig(_myself.value());
        else {
            d_err("cannot find configuration of this node");
            exit(-1);
        }
    }

    auto idSet = clusterConf->getNodeIdSet();
    for (int id : idSet)
        peerIsAlive[id] = true;                 /* FIXME?: Assume true at start */

    socket = new RDMASocket();

    rpcListener = std::thread(&RPCInterface::rpcListen, this);
    rpcListener.detach();
}

/*
 * Destructor deallocates `clusterConf` and `myNodeConf`.
 */
RPCInterface::~RPCInterface()
{
    delete socket;
    socket = nullptr;

    if (cmdConf) {
        delete cmdConf;
        cmdConf = nullptr;
    }
    if (myNodeConf) {
        delete myNodeConf;
        myNodeConf = nullptr;
    }
}

void RPCInterface::rpcListen()
{
    ibv_wc wc;
    while (isRunning.load()) {
        int ret = socket->pollCompletion(&wc);
        if (ret < 0) {
            d_err("failed to poll CQ, stop");
            return;
        }

        if (wc.opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
            int peerId = wc.imm_data;
            auto *message = reinterpret_cast<RPCMessage *>(memConf->getReceiveBuffer(peerId));
            auto *response = reinterpret_cast<RPCMessage *>(memConf->getSendBuffer(peerId));
            if (rpcProcessCall(peerId, message, response) < 0) {
                d_err("cannot process RPC request from peer: %d, stop", peerId);
                return;
            }
            
            /* Repost RDMA receive before responding for maybe better performance */
            socket->postReceive(peerId, 0);

            /* Use RDMA write to send reply. TO BE EXAMINED */
            if (socket->postWrite(peerId, memConf->getReceiveBufferShift(myNodeConf->id),
                    (uint64_t)response, sizeof(RPCMessage), myNodeConf->id) < 0) {
                d_err("cannot write reply to peer: %d, stop", peerId);
                return;
            }
        }
        else
            d_warn("unknown opcode: %d, ignore", (int)wc.opcode);
    }
}

int RPCInterface::rpcProcessCall(int peerId, const RPCMessage *message, RPCMessage *response)
{
    memset(response, 0, sizeof(RPCMessage));
    switch (message->type) {
        case RPC_ALLOC: {
            //uint64_t ret = remoteAllocBlock(peerId);
            uint64_t ret = 0;
            if (ret < 0) {
                d_warn("cannot alloc block for peer: %d", peerId);
                response->type = RPC_RESPONSE_NAK;
            }
            else {
                response->type = RPC_RESPONSE_ACK;
                response->addr = ret;
                response->count = 1;
            }
            return 0;
        }
        case RPC_DEALLOC: {
            //int ret = remoteDeallocBlock(peerId, message->addr);
            int ret = 0;
            if (ret < 0) {
                d_err("failed to dealloc block for peer: %d", peerId);
                response->type = RPC_RESPONSE_NAK;
            }
            else
                response->type = RPC_RESPONSE_ACK;
            return 0;
        }
        default:
            break;
    }
    return -1;
}

int RPCInterface::remoteRPCCall(int peerId, const RPCMessage *request, RPCMessage *response)
{
    auto *sendBuf = reinterpret_cast<RPCMessage *>(memConf->getSendBuffer(peerId));
    memcpy(sendBuf, request, sizeof(RPCMessage));

    sendBuf->uid = taskId.fetch_add(1);
    __mem_clflush(sendBuf);

    auto *recvBuf = reinterpret_cast<RPCMessage *>(memConf->getReceiveBuffer(peerId));
    recvBuf->type = 0;
    __mem_clflush(recvBuf);

    if (socket->postSend(peerId, (uint64_t)sendBuf, sizeof(RPCMessage)) < 0) {
        d_err("cannot send RPC call via RDMA (to peer: %d)", peerId);
        return -1;
    }
    
    while ((recvBuf->type & RPC_RESPONSE) == 0);
    memcpy(response, recvBuf, sizeof(RPCMessage));
    return 0;
}
