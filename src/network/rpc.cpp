#include "rpc.hpp"
#include "debug.hpp"

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

    if (cmdConf != nullptr) {
        delete cmdConf;
        cmdConf = nullptr;
    }
    if (myNodeConf != nullptr) {
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
    return 0;
}

int RPCInterface::remoteRPCCall(int peerId, const RPCMessage *request, RPCMessage *response)
{
    auto *sendBuf = reinterpret_cast<RPCMessage *>(memConf->getSendBuffer(peerId));
    memcpy(sendBuf, request, sizeof(RPCMessage));

    sendBuf->uid = taskId.fetch_add(1);
    mem_force_flush(sendBuf);

    auto *recvBuf = reinterpret_cast<RPCMessage *>(memConf->getReceiveBuffer(peerId));
    recvBuf->type = 0;
    mem_force_flush(recvBuf);

    if (socket->postSend(peerId, (uint64_t)sendBuf, sizeof(RPCMessage)) < 0) {
        d_err("cannot send RPC call via RDMA (to peer: %d)", peerId);
        return -1;
    }
    
    while ((recvBuf->type & RPC_RESPONSE) == 0);
    memcpy(response, recvBuf, sizeof(RPCMessage));
    return 0;
}
