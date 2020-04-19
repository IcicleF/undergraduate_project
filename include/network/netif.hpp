#if !defined(NETIF_HPP)
#define NETIF_HPP

#include "rpc.h"            // eRPC
#include "rdma.hpp"         // self-written RDMA
#include "msg.hpp"          // new RPC message buffer definitions

enum class ErpcType
{
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

template <int NBits> struct Bits2Type     { };
template <>          struct Bits2Type<8>  { using type = uint8_t;  };
template <>          struct Bits2Type<16> { using type = uint16_t; };
template <>          struct Bits2Type<32> { using type = uint32_t; };
template <>          struct Bits2Type<64> { using type = uint64_t; }; 

template <typename Int>
inline typename std::enable_if<std::is_integral<Int>::value && (sizeof(Int) <= sizeof(int)), int>::type ffs(Int x)
{
    return __builtin_ffs(x) - 1;
}

template <typename Int>
inline typename std::enable_if<std::is_integral<Int>::value && (sizeof(Int) > sizeof(int)), int>::type ffs(Int x)
{
    return __builtin_ffsl(x) - 1;
}

void smHandler(int sessionNum, erpc::SmEventType event, erpc::SmErrType err, void *context);
void contFunc(void *context, void *tag);

class NetworkInterface
{
    static const int NLockers = 32;

    friend void smHandler(int, erpc::SmEventType, erpc::SmErrType, void *);
    friend void contFunc(void *, void *);

public:
    explicit NetworkInterface(const std::unordered_map<int, erpc::erpc_req_func_t> &rpcProcessors)
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

        rdma = std::make_unique<RDMASocket>();

        std::string serverURI = myNodeConf->hostname + ":" + std::to_string(cmdConf->udpPort);
        nexus = std::make_unique<erpc::Nexus>(serverURI, 0, 0);
        for (auto v : rpcProcessors)
            nexus->register_req_func(v.first, v.second);
        rpc = std::make_unique<erpc::Rpc<erpc::CTransport>>(nexus.get(), this, 0, smHandler);

        for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
            NodeConfig conf = (*clusterConf)[i];
            if (conf.id == myNodeConf->id)
                continue;
            if ((!cmdConf->recover && conf.id < myNodeConf->id) || cmdConf->recover) {
                std::string uri = conf.hostname + ":" + std::to_string(cmdConf->udpPort);
                sessions[conf.id] = rpc->create_session(uri, 0);
                while (!rpc->is_connected(sessions[conf.id]))
                    rpc->run_event_loop_once();
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

    inline void rdmaRead(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length, uint32_t taskId = 0)
    {
        rdma->postRead(peerId, remoteSrcShift, localDst, length, taskId);
    }
    inline void rdmaWrite(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm = -1)
    {
        rdma->postWrite(peerId, remoteDstShift, localSrc, length, imm);
    }

    template <typename ReqTy, typename RespTy>
    bool rpcCall(int peerId, ErpcType type, const ReqTy &req, RespTy &resp)
    {
        erpc::MsgBuffer reqBuf = rpc->alloc_msg_buffer_or_die(sizeof(ReqTy));
        memcpy(reqBuf.buf, &req, sizeof(ReqTy));
        
        int idx = allocBit();
        if (idx == -1)
            return false;
        
        rpc->enqueue_request(sessions[peerId], static_cast<int>(type), &reqBuf, &locks[idx].respBuf,
                             contFunc, reinterpret_cast<void *>(idx));
        locks[idx].wait();
        freeBit(idx);

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

    int allocBit()
    {
        BitmapTy origin = bitmap.load();
        while (origin) {
            BitmapTy lowbit = origin & -origin;
            if (bitmap.compare_exchange_weak(origin, origin & ~lowbit))
                return ffs(lowbit);
        }
        return -1;
    }
    void freeBit(int idx)
    {
        BitmapTy bit = static_cast<BitmapTy>(1) << idx;
        while (true) {
            BitmapTy origin = bitmap.load();
            if (bitmap.compare_exchange_weak(origin, origin | bit))
                return;
        }
    }

    std::shared_ptr<erpc::Nexus> nexus;
    std::shared_ptr<erpc::Rpc<erpc::CTransport>> rpc;
    std::shared_ptr<RDMASocket> rdma;

    std::atomic<bool> shouldRun;
    std::thread listener;

    int sessions[MAX_NODES];
    std::unordered_map<int, int> sess2id;

    Locker locks[NLockers];
    std::atomic<BitmapTy> bitmap;
};

#endif // NETIF_HPP
