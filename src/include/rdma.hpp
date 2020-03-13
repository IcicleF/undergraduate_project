#if !defined(RDMA_HPP)
#define RDMA_HPP

#include <unistd.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "config.hpp"
#include "debug.hpp"

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htonll(x) bswap_64((uint64_t)(x))
#define ntohll(x) bswap_64((uint64_t)(x))
#elif __BYTE_ORDER == __BIG_ENDIAN
#define htonll(x) ((uint64_t)(x))
#define ntohll(x) ((uint64_t)(x))
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

/* Structure to exchange data which is needed to connect the QPs */
struct ConnInfo
{
    uint64_t baseAddr;                  /* REMOTE RDMA buffer (memConf->base)
                                           Use `baseAddr` for RDMA read/write */
    uint32_t nodeId;                    /* Remote Node ID */
    uint32_t rkey;                      /* Remote key */
    uint32_t qpn;                       /* QP number */
    uint16_t lid;                       /* LID of the IB port */
} __packed;

/* Store the RDMA connection data with a peer */
struct PeerInfo
{
    union
    {
        ConnInfo connInfo;              /* Connection info received from peer */
        struct                          /* Provide direct access to `connInfo` members */
        {
            uint64_t baseAddr;
            uint32_t nodeId;
            uint32_t rkey;
            uint32_t qpn;
            uint16_t lid;
        } __packed;
    } __packed;

    ibv_qp *qp = nullptr;               /* QP handle */
    ibv_cq *cq = nullptr;               /* CQ handle */
    int sock;                           /* TCP socket */
    void *sendBuf = nullptr;            /* LOCAL RDMA send buffer */
    void *recvBuf = nullptr;            /* LOCAL RDMA receive buffer */
};

/* Store all necessary resources for RDMA connection with other nodes */
class RDMASocket
{
public:
    explicit RDMASocket();
    RDMASocket(const RDMASocket &) = delete;
    RDMASocket &operator=(const RDMASocket &) = delete;
    ~RDMASocket();

    int createResources();
    void disposeResources();

    int rdmaConnect(int peerId);
    void verboseQP(int peerId);

    int postSend(int peerId, uint64_t localSrc, uint64_t length);
    int postReceive(int peerId, uint64_t length);
    int postWrite(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm = -1);
    int postRead(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length);

    int pollCompletion(ibv_wc *wc);
    int pollOnce(ibv_wc *wc);
    bool ready() { rdmaListener.join(); return true; }
    void stop()
    {
        char buf[] = "STOP", remote[10];
        for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
            if (i == myNodeConf->id)
                continue;
            socketExchangeData(rs.peers[i].sock, 4, buf, remote);
            if (strncmp(buf, remote, 4) != 0)
                d_err("TAT: %d", i);
            else
                d_info("stopped: %d", i);
        }
        d_info("successfully sync stopped RDMASocket");
    }

private:
    int socketExchangeData(int sock, int size, void *localData, void *remoteData);
    int socketConnect(int peerId);
    int createQP(PeerInfo *peer);
    int connectQP(PeerInfo *peer);
    int modifyQPtoInit(PeerInfo *peer);
    int modifyQPtoRTR(PeerInfo *peer);
    int modifyQPtoRTS(PeerInfo *peer);

    void rdmaAccept(int sock);
    int rdmaListen(int port);

private:
    struct
    {
        ibv_device_attr deviceAttr;         /* Device attributes */
        ibv_port_attr portAttr;             /* IB port attributes */
        ibv_context *context = nullptr;     /* device handle */
        ibv_pd *pd = nullptr;               /* PD handle */
        ibv_cq *cq = nullptr;               /* CQ handle */
        ibv_mr *mr = nullptr;               /* MR handle */

        PeerInfo peers[MAX_NODES];          /* Peer connections */
    } rs;
    std::thread rdmaListener;
    bool listenerQuit = false;              /* if true, then ready */
};

#endif // RDMA_HPP
