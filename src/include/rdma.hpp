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

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "config.hpp"
#include "message.hpp"
#include "hashtable.hpp"

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

/*
 * Store all necessary resources for RDMA connection with other nodes.
 * 
 *   RDMASocket uses RDMA CM APIs to build RDMA reliable connections (RCs) with peers.
 *   TCP must be available for the RDMA CM to build connections.
 * 
 *   Before posting any RDMA send/recv/read/write requests, a HashTable instance must
 * be registered. Unless specified, a unique ID will be automatically assigned to each
 * request (and is returned).
 *   Hash table entries will be set upon WR completion.
 *   Users must manually free this ID to avoid filling the hash table full. However,
 * there is always room for special IDs and they MUST NOT be freed.
 */
class RDMASocket
{
public:
    explicit RDMASocket();
    ~RDMASocket();
    RDMASocket(const RDMASocket &) = delete;
    RDMASocket &operator=(const RDMASocket &) = delete;

    void registerHashTable(HashTable *hashTable);
    void verboseQP(int peerId);
    bool isPeerAlive(int peerId);
    void stopListenerAndJoin();

    uint32_t postSend(int peerId, uint64_t length, int specialTaskId = -1);
    uint32_t postReceive(int peerId, uint64_t length, int specialTaskId = -1);
    uint32_t postWrite(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length,
                       int imm = -1, int specialTaskId = -1);
    uint32_t postRead(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length,
                      int specialTaskId = -1);

    int pollRecvCompletion(ibv_wc *wc);
    int pollRecvOnce(ibv_wc *wc);

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

    void listenSendCQ();                    /* listen cq[CQ_SEND]: threaded */

    HashTable *hashTable = nullptr;         /* Record completion status */

    ibv_context *ctx = nullptr;
    ibv_pd *pd = nullptr;                   /* Common protection domain */
    ibv_mr *mr = nullptr;                   /* Common memory region */
    ibv_cq *cq[MAX_CQS];                    /* cq[0]: ibv_post_send (acknowlegde)
                                             * cq[1]: ibv_post_recv (event) */
    ibv_comp_channel *compChannel[MAX_CQS]; /* Complete channel array */
    std::thread cqSendPoller;               /* listenSendCQ thread */

    rdma_event_channel *ec = nullptr;       /* Common RDMA event channel */
    rdma_cm_id *listener = nullptr;         /* RDMA listener */
    std::thread ecPoller;                   /* listenRDMAEvents thread */

    RDMAConnection peers[MAX_NODES];        /* Peer connections */
    std::map<uint64_t, int> cm2id;          /* Map rdma_cm_id pointer to peer */
    uint32_t nodeIDBuf;                     /* Send my node ID on connection */

    bool shouldRun;                         /* Stop threads if false */
};

#endif // RDMA_HPP
