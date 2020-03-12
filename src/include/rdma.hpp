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
#include "message.hpp"

struct RDMAConnection
{
    int peerId;
    bool connected;

    rdma_cm_id *cmId;                   /* CM: allocated */
    ibv_qp *qp;                         /* QP: allocated */

    ibv_mr *sendMR;                     /* Send Region MR */
    ibv_mr *recvMR;                     /* Recv Region MR */
    ibv_mr peerMR;                      /* MR of peer */
    
    uint8_t *sendRegion;                /* Send Region: allocated */
    uint8_t *recvRegion;                /* Recv Region: allocated */
};

#if 0

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

#endif

/* Store all necessary resources for RDMA connection with other nodes */
class RDMASocket
{
public:
    explicit RDMASocket();
    ~RDMASocket();
    RDMASocket(const RDMASocket &) = delete;
    RDMASocket &operator=(const RDMASocket &) = delete;

    void verboseQP(int peerId);
    bool isPeerAlive(int peerId);

    void postSend(int peerId, uint32_t taskId, uint64_t length);
    void postReceive(int peerId, uint32_t taskId, uint64_t length);
    void postWrite(int peerId, uint32_t taskId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm = -1);
    void postRead(int peerId, uint32_t taskId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length);

    int pollCompletion(ibv_wc *wc);
    int pollOnce(ibv_wc *wc);

private:
    void listenRDMAEvents();
    void onAddrResolved(rdma_cm_event *event);
    void onRouteResolved(rdma_cm_event *event);
    void onConnectionRequest(rdma_cm_event *event);
    void onConnectionEstablished(rdma_cm_event *event);
    void onDisconnected(rdma_cm_event *event);
    void onCompletion(ibv_wc *wc);

    void buildResources(ibv_context *ctx);
    void buildConnection(rdma_cm_id *cmId);
    void buildConnParam(rdma_conn_param *param);
    void destroyConnection(rdma_cm_id *cmId);

    void listenCQ(int index);

    ibv_context *ctx = nullptr;
    ibv_pd *pd = nullptr;                   /* Common protection domain */
    ibv_mr *mr = nullptr;                   /* Common memory region */
    ibv_cq *cq[MAX_CQS];                    /* CQ array */
    ibv_comp_channel *compChannel[MAX_CQS]; /* Complete channel array */
    std::thread cqPoller[MAX_CQS];          /* listenCQ thread */

    rdma_event_channel *ec = nullptr;       /* Common RDMA event channel */
    rdma_cm_id *listener = nullptr;         /* RDMA listener */
    std::thread ecPoller;                   /* listenRDMAEvents thread */

    RDMAConnection peers[MAX_NODES];        /* Peer connections */
    std::map<uint64_t, int> cm2id;          /* Map rdma_cm_id pointer to peer */
    uint32_t nodeIDBuf;                     /* Send my node ID on connection */

    bool shouldRun;
};

#endif // RDMA_HPP
