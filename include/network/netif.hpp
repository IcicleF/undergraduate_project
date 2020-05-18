#if !defined(NETIF_HPP)
#define NETIF_HPP

#include "rpc.h"            // eRPC
#include "rdma.hpp"         // self-written RDMA
#include "msg.hpp"          // new RPC message buffer definitions
#include "../bitmap.hpp"
#include "../debug.hpp"

enum class ErpcType
{
    ERPC_CONNECT,
    ERPC_DISCONNECT,
    ERPC_TEST,
    ERPC_OPEN,
    ERPC_ACCESS,
    ERPC_CREATE,
    ERPC_READ,
    ERPC_WRITE,
    ERPC_REMOVE,
    ERPC_FILESTAT,
    ERPC_DIRSTAT,
    ERPC_MKDIR,
    ERPC_RMDIR,
    ERPC_OPENDIR,
    ERPC_READDIR
};

void smHandler(int sessionNum, erpc::SmEventType event, erpc::SmErrType err, void *context);
void contFunc(void *context, void *tag);
void connectHandler(erpc::ReqHandle *reqHandle, void *context);

/* ERPC Interface */
class NetworkInterface
{
    static const int NLockers = 32;

    friend void smHandler(int, erpc::SmEventType, erpc::SmErrType, void *);
    friend void contFunc(void *, void *);
    friend void connectHandler(erpc::ReqHandle *, void *);

public:
    explicit NetworkInterface(const std::unordered_map<int, erpc::erpc_req_func_t> &rpcProcessors)
    {
        std::string serverURI = myNodeConf->hostname + ":" + std::to_string(cmdConf->udpPort);
        nexus = std::make_unique<erpc::Nexus>(serverURI, 0, 0);
        for (auto v : rpcProcessors)
            nexus->register_req_func(v.first, v.second);
        nexus->register_req_func(static_cast<int>(ErpcType::ERPC_CONNECT), connectHandler);
        rpc = std::make_unique<erpc::Rpc<erpc::CTransport>>(nexus.get(), this, 0, smHandler);

        /* Servers do not need to connect to other nodes */
        if (static_cast<int>(myNodeConf->type) & NODE_SERVER)
            return;

        /* Clients should connect to servers */
        for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
            NodeConfig conf = (*clusterConf)[i];
            if ((static_cast<int>(conf.type) & NODE_SERVER) == 0)
                continue;
            if ((!cmdConf->recover && conf.id < myNodeConf->id) || cmdConf->recover) {
                std::string uri = conf.hostname + ":" + std::to_string(cmdConf->udpPort);
                int sess = sessions[conf.id] = rpc->create_session(uri, 0);
                sess2id[sess] = conf.id;
                while (!rpc->is_connected(sessions[conf.id]))
                    rpc->run_event_loop_once();

                /* Send an RPC call to inform server of my identity */
                {
                    /* Wait to avoid remote unordered_map corruption */
                    std::this_thread::sleep_for(std::chrono::microseconds(10));

                    PureValueRequest notifyReq;
                    PureValueResponse notifyResp;
                    notifyReq.value = myNodeConf->id;
                    notifyResp.value = -1;
                    rpcCall(conf.id, ErpcType::ERPC_CONNECT, notifyReq, notifyResp);
                    if (notifyResp.value < 0)
                        d_err("failed to notify ID to peer: %d", conf.id);
                }
            }
        }

        for (int i = 0; i < NLockers; ++i)
            locks[i].respBuf = rpc->alloc_msg_buffer_or_die(sizeof(GeneralResponse));

        shouldRun.store(true);
        listener = std::thread(&NetworkInterface::rpcListen, this);
    }
    ~NetworkInterface()
    {
        shouldRun.store(false);
        if (listener.joinable())
            listener.join();
        
        for (int i = 0; i < NLockers; ++i)
            rpc->free_msg_buffer(locks[i].respBuf);
    }

    template <typename ReqTy, typename RespTy>
    bool rpcCall(int peerId, ErpcType type, const ReqTy &req, RespTy &resp)
    {
        erpc::MsgBuffer reqBuf = rpc->alloc_msg_buffer_or_die(sizeof(ReqTy));
        memcpy(reqBuf.buf, &req, sizeof(ReqTy));
        
        int idx = bitmap.allocBit();
        if (idx == -1)
            return false;
        
        rpc->enqueue_request(sessions[peerId], static_cast<int>(type), &reqBuf, &locks[idx].respBuf,
                             contFunc, reinterpret_cast<void *>(idx));
        locks[idx].wait();
        bitmap.freeBit(idx);

        memcpy(&resp, locks[idx].respBuf.buf, sizeof(RespTy));
        return true;
    }

    void rpcListen()
    {
        while (shouldRun.load())
            rpc->run_event_loop(1000);
    }

private:
    using BitmapTy = typename Bits2Type<NLockers>::type;
    struct Locker
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool completed;
        erpc::MsgBuffer respBuf; 

        explicit Locker() = default;
        void wait()
        {   
            std::unique_lock<std::mutex> lock(mutex);
            completed = false;
            while (!completed)
                cv.wait(lock);
        }
        void complete()
        {
            std::unique_lock<std::mutex> lock(mutex);
            completed = true;
            cv.notify_one();
        }
    };

    std::unique_ptr<erpc::Nexus> nexus;
    std::unique_ptr<erpc::Rpc<erpc::CTransport>> rpc;

    std::atomic<bool> shouldRun;
    std::thread listener;

    int sessions[MAX_NODES];
    std::unordered_map<int, int> sess2id;

    Locker locks[NLockers];
    Bitmap<NLockers> bitmap;
};

#endif // NETIF_HPP
