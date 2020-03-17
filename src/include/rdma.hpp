/*
 * rdma.hpp
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Defines an RDMA interface to support RDMA send/recv/write/read primitives.
 * It hides RDMA details from users and notify user WR results with a hash table. 
 */

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
#include <condition_variable>

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "config.hpp"
#include "message.hpp"

/* Store necessary information for a connection with a peer. */
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

/* Predeclaration for RDMASocket to befriend it */
class RPCInterface;

/*
 * Store all necessary resources for RDMA connection with other nodes.
 * 
 * RDMASocket uses RDMA CM APIs to build RDMA reliable connections (RCs) with peers.
 * TCP must be available for the RDMA CM to build connections.
 */
class RDMASocket
{
    friend class RPCInterface;

public:
    explicit RDMASocket();
    ~RDMASocket();
    RDMASocket(const RDMASocket &) = delete;
    RDMASocket &operator=(const RDMASocket &) = delete;

    void verboseQP(int peerId);
    bool isPeerAlive(int peerId);
    void stopListenerAndJoin();

    void postSend(int peerId, uint64_t length);
    void postReceive(int peerId, uint64_t length, int specialTaskId = 0);
    void postWrite(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm = -1);
    void postRead(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length, uint32_t taskId = 0);

    int pollSendCompletion(ibv_wc *wc);
    int pollRecvCompletion(ibv_wc *wc);

private:
    void listenRDMAEvents();
    void onAddrResolved(rdma_cm_event *event);
    void onRouteResolved(rdma_cm_event *event);
    void onConnectionRequest(rdma_cm_event *event);
    void onConnectionEstablished(rdma_cm_event *event);
    void onDisconnected(rdma_cm_event *event);
    void onSendCompletion(ibv_wc *wc);

    void buildResources(ibv_context *ctx);
    void buildConnection(rdma_cm_id *cmId);
    void buildConnParam(rdma_conn_param *param);
    void destroyConnection(rdma_cm_id *cmId);

    ibv_context *ctx = nullptr;
    ibv_pd *pd = nullptr;                   /* Common protection domain */
    ibv_mr *mr = nullptr;                   /* Common memory region */
    ibv_cq *cq[MAX_CQS];                    /* [0]: send CQ; [1]: recv CQ */
    ibv_comp_channel *compChannel[MAX_CQS]; /* [0]: send channel; [1]: recv channel */

    rdma_event_channel *ec = nullptr;       /* Common RDMA event channel */
    rdma_cm_id *listener = nullptr;         /* RDMA listener */
    std::thread ecPoller;                   /* listenRDMAEvents thread */

    RDMAConnection peers[MAX_NODES];        /* Peer connections */
    std::map<uint64_t, int> cm2id;          /* Map rdma_cm_id pointer to peer */
    uint32_t nodeIDBuf;                     /* Send my node ID on connection */

    bool shouldRun;                         /* Stop threads if false */
    int incomingConns = 0;                  /* Incoming successful connections count */

    std::mutex stopSpinMutex;               /* To stop ctor from spinning */
    std::condition_variable ssmCondVar;     /* To stop ctor from spinning */
};

#define WRID(p, t)      ((((uint64_t)(p)) << 32) | ((uint64_t)(t)))
#define WRID_PEER(id)   ((int)(((id) >> 32) & 0xFFFFFFFF))
#define WRID_TASK(id)   ((uint32_t)((id) & 0xFFFFFFFF))

#endif // RDMA_HPP
