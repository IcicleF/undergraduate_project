#if !defined(RDMA_HPP)
#define RDMA_HPP

#include <unistd.h>
#include <inttypes.h>
#include <byteswap.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "config.hpp"

struct RDMAConnection
{
    int peerId;
    bool connected;

    ibv_qp *qp;
    ibv_cq *cq;
    ibv_comp_channel *compChannel;
    pthread_t cqPoller;

    uint8_t *sendRegion;
    uint8_t *recvRegion;
};

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
    ~RDMASocket();
    RDMASocket(const RDMASocket &) = delete;
    RDMASocket(const RDMASocket &&) = delete;
    RDMASocket &operator=(const RDMASocket &) = delete;

    int rdmaConnect(int peerId);
    void verboseQP(int peerId);

    int postSend(int peerId, uint64_t localSrc, uint64_t length);
    int postReceive(int peerId, uint64_t length);
    int postWrite(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm = -1);
    int postRead(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length);

    int pollCompletion(ibv_wc *wc);
    int pollOnce(ibv_wc *wc);

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
#if 0
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
#endif
    rdma_event_channel *ec = nullptr;       /* Common RDMA event channel */
    rdma_cm_id *cm = nullptr;               /* Common RDMA connection manager */
    ibv_pd *pd = nullptr;                   /* Common protection domain */
    ibv_mr *mr = nullptr;                   /* Common memory region */
    ibv_cq *cq[MAX_CQS];                    /* CQ array */
};

#endif // RDMA_HPP
