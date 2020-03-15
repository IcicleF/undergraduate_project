#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <config.hpp>
#include <debug.hpp>
#include <rdma.hpp>

RDMASocket::RDMASocket()
{
    if (!cmdConf || !clusterConf || !memConf || !myNodeConf) {
        d_err("all configurations should be initialized!");
        exit(-1);
    }

    shouldRun = true;
    nodeIDBuf = myNodeConf->id;

    sockaddr_in addr;
    memset(&addr, 0, sizeof(sockaddr));
    addr.sin_family = AF_INET;

    for (int i = 0; i < MAX_NODES; ++i) {
        peers[i].cmId = nullptr;
        peers[i].qp = nullptr;
        peers[i].sendMR = nullptr;
        peers[i].recvMR = nullptr;
        peers[i].sendRegion = nullptr;
        peers[i].recvRegion = nullptr;
        peers[i].connected = false;
    }

    expectNonZero(ec = rdma_create_event_channel());
    expectZero(rdma_create_id(ec, &listener, nullptr, RDMA_PS_TCP));
    expectZero(rdma_bind_addr(listener, reinterpret_cast<sockaddr *>(&addr)));
    expectZero(rdma_listen(listener, MAX_NODES));

    int port = ntohs(rdma_get_src_port(listener));
    char portStr[8];
    snprintf(portStr, 8, "%d", port);
    d_info("listening on port: %d", port);

    /* Connect to all peers with id < myId */
    for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
        auto peerNode = (*clusterConf)[i];
        if (peerNode.id >= myNodeConf->id)
            continue;

        addrinfo *ai;
        getaddrinfo(peerNode.ipAddrStr.c_str(), portStr, nullptr, &ai);

        expectZero(rdma_create_id(ec, &peers[i].cmId, nullptr, RDMA_PS_TCP));
        cm2id[(uint64_t)peers[i].cmId] = i;
        expectZero(rdma_resolve_addr(peers[i].cmId, nullptr, ai->ai_addr, ADDR_RESOLVE_TIMEOUT));

        freeaddrinfo(ai);
    }

    int expectedConns = 0;
    for (int i = 0; i < clusterConf->getClusterSize(); ++i)
        if ((*clusterConf)[i].id > myNodeConf->id)
            ++expectedConns;

    ecPoller = std::thread(&RDMASocket::listenRDMAEvents, this);

    d_info("expect %d incoming connections", expectedConns);
    if (expectedConns) {
        d_info("start waiting...");
        while (incomingConns < expectedConns) ;
    }

    d_info("successfully created RDMASocket!");
}

RDMASocket::~RDMASocket()
{
    stopListenerAndJoin();

    for (int i = 0; i < MAX_NODES; ++i) {
        if (peers[i].qp) 
            ibv_destroy_qp(peers[i].qp);
        if (peers[i].cmId)
            rdma_destroy_id(peers[i].cmId);
    }
    for (int i = 0; i < MAX_CQS; ++i)
        if (cq[i])
            ibv_destroy_cq(cq[i]);
    
    if (mr)
        ibv_dereg_mr(mr);
    if (pd)
        ibv_dealloc_pd(pd);
    if (listener)
        rdma_destroy_id(listener);
    if (ec)
        rdma_destroy_event_channel(ec);
}


/* Register a hash table to record CQ completion information. */
void RDMASocket::registerHashTable(HashTable *hashTable)
{
    this->hashTable = hashTable;
}

/* Stop all listener threads and join them to the current thread */
void RDMASocket::stopListenerAndJoin()
{
    if (std::this_thread::get_id() != mainThreadId) {
        d_err("cannot execute stopListenerAndJoin from non-main threads");
        return;
    }
    if (!shouldRun)
        return;

    shouldRun = false;
    if (cqSendPoller.joinable())
        cqSendPoller.join();
    if (ecPoller.joinable())
        ecPoller.join();
    
    d_info("all joinable listener threads have joined");
}

/* This function is expected to run as a single thread. */
void RDMASocket::listenRDMAEvents()
{
    typedef void (RDMASocket:: *EventHandler)(rdma_cm_event *);
    static EventHandler handlers[16] = { nullptr };
    handlers[RDMA_CM_EVENT_ADDR_RESOLVED] = &RDMASocket::onAddrResolved;
    handlers[RDMA_CM_EVENT_ROUTE_RESOLVED] = &RDMASocket::onRouteResolved;
    handlers[RDMA_CM_EVENT_CONNECT_REQUEST] = &RDMASocket::onConnectionRequest;
    handlers[RDMA_CM_EVENT_ESTABLISHED] = &RDMASocket::onConnectionEstablished;
    handlers[RDMA_CM_EVENT_DISCONNECTED] = &RDMASocket::onDisconnected;

    rdma_cm_event *event;
    while (shouldRun && rdma_get_cm_event(ec, &event) == 0) {
        EventHandler handler = handlers[event->event];
        if (handler)
            (this->*handler)(event);
        else
            d_warn("RDMA CM event type %d not handled", (int)event->event);
        rdma_ack_cm_event(event);
    }
}

/* As a client, handle when remote address is resolved */
void RDMASocket::onAddrResolved(rdma_cm_event *event)
{
    buildConnection(event->id);
    expectZero(rdma_resolve_route(event->id, ADDR_RESOLVE_TIMEOUT));
}

/* As a client, handle when connection route is resolved */
void RDMASocket::onRouteResolved(rdma_cm_event *event)
{
    rdma_conn_param param;
    buildConnParam(&param);
    expectZero(rdma_connect(event->id, &param));
}

/* As a server, handle when an incoming connection request appears */
void RDMASocket::onConnectionRequest(rdma_cm_event *event)
{
    auto *nodeIdBuf = reinterpret_cast<const uint32_t *>(event->param.conn.private_data);
    cm2id[(uint64_t)event->id] = nodeIdBuf[0];
    buildConnection(event->id);

    rdma_conn_param param;
    buildConnParam(&param);

    expectZero(rdma_accept(event->id, &param));
}

/* As a server or client, handle when a connection is established */
void RDMASocket::onConnectionEstablished(rdma_cm_event *event)
{
    RDMAConnection *peer = peers + cm2id[(uint64_t)event];
    expectTrue(peer == reinterpret_cast<RDMAConnection *>(event->id->context));

    /* Send MR to peer */
    auto *mrMsg = reinterpret_cast<Message *>(peer->sendRegion);
    mrMsg->type = Message::MESG_REMOTE_MR;
    memcpy(mr, &mrMsg->data.mr, sizeof(ibv_mr));
    postSend(peer->peerId, sizeof(Message), SP_REMOTE_MR_SEND);

    /*
     * Immediately (and blockingly) poll this RDMA recv.
     * Because the connection is already established, we expect not blocking very long.
     */
    ibv_wc wc;
    expectPositive(pollRecvCompletion(&wc));
    
    if (wc.opcode == IBV_WC_RECV_RDMA_WITH_IMM && WRID_TASK(wc.wr_id) == SP_REMOTE_MR_RECV) {
        auto *msg = reinterpret_cast<Message *>(peer->recvRegion);
        if (msg->type == Message::MESG_REMOTE_MR) {
            memcpy(&peer->peerMR, &msg->data.mr, sizeof(ibv_mr));
            peer->connected = true;    
            d_info("successfully connected with peer: %d", peer->peerId);
        }
        else {
            d_err("RDMA recv intended for MR received some other thing");
            exit(-1);
        }
    }
}

/* As a server or client, handle when a connection is lost */
void RDMASocket::onDisconnected(rdma_cm_event *event)
{
    destroyConnection(event->id);
}

void RDMASocket::onSendCompletion(ibv_wc *wc)
{
    int peerId = WRID_PEER(wc->wr_id);
    uint32_t taskId = WRID_TASK(wc->wr_id);

    RDMAConnection *peer = peers + peerId;
    if (wc->status != IBV_WC_SUCCESS) {
        d_err("wc failed (peer: %d, op: %d, err: %d)",
                peer->peerId, (int)wc->opcode, (int)wc->status);
        return;
    }
    if (wc->opcode & IBV_WC_RECV) {
        d_err("fatal: onSendCompletion polled recv completion");
        exit(-1);
    }

    /* RDMA write is not polled by user, process here */
    if (wc->opcode == IBV_WC_RDMA_WRITE) {
        hashTable->freeID(taskId);
        return;
    }

    /* Store the data in hash table for user polling */
    (*hashTable)[taskId] = 1;
}

void RDMASocket::buildResources(ibv_context *ctx)
{
    if (this->ctx) {
        if (this->ctx != ctx)
            d_err("more than one context...");
        return;
    }

    this->ctx = ctx;
    expectNonZero(pd = ibv_alloc_pd(this->ctx));
    for (int i = 0; i < MAX_CQS; ++i) {
        expectNonZero(compChannel[i] = ibv_create_comp_channel(this->ctx));
        expectNonZero(cq[i] = ibv_create_cq(this->ctx, MAX_QP_DEPTH, nullptr, compChannel[i], 0));
        expectZero(ibv_req_notify_cq(cq[i], 0));
    }
    cqSendPoller = std::thread(&RDMASocket::listenSendCQ, this);
    int mrFlags = IBV_ACCESS_LOCAL_WRITE |
                  IBV_ACCESS_REMOTE_READ |
                  IBV_ACCESS_REMOTE_WRITE |
                  IBV_ACCESS_REMOTE_ATOMIC;
    expectNonZero(mr = ibv_reg_mr(pd, memConf->getMemory(), memConf->getCapacity(), mrFlags));
}

void RDMASocket::buildConnection(rdma_cm_id *cmId)
{
    buildResources(cmId->verbs);

    ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(ibv_qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = cq[CQ_SEND];
    qp_init_attr.recv_cq = cq[CQ_RECV];
    qp_init_attr.cap.max_send_wr = MAX_QP_DEPTH;
    qp_init_attr.cap.max_recv_wr = MAX_QP_DEPTH;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    expectZero(rdma_create_qp(cmId, pd, &qp_init_attr));

    int peerId = cm2id[(uint64_t)cmId];
    RDMAConnection *peer = peers + peerId;
    peer->peerId = peerId;
    peer->cmId = cmId;
    peer->qp = cmId->qp;
    peer->connected = false;
    peer->sendRegion = new uint8_t[RDMA_BUF_SIZE];
    peer->recvRegion = new uint8_t[RDMA_BUF_SIZE];

    expectNonZero(peer->sendMR = ibv_reg_mr(pd, peer->sendRegion, RDMA_BUF_SIZE, 0));
    expectNonZero(peer->recvMR = ibv_reg_mr(pd, peer->recvRegion, RDMA_BUF_SIZE, IBV_ACCESS_LOCAL_WRITE));
    
    cmId->context = reinterpret_cast<void *>(peers + peerId);

    /* Wait for remote MR */
    postReceive(peerId, sizeof(Message), SP_REMOTE_MR_RECV);
}

void RDMASocket::buildConnParam(rdma_conn_param *param)
{
    memset(param, 0, sizeof(rdma_conn_param));
    param->initiator_depth = MAX_REQS;
    param->responder_resources = MAX_REQS;
    param->rnr_retry_count = 7;             /* infinite retry */
    param->private_data = reinterpret_cast<const void *>(&nodeIDBuf);
    param->private_data_len = sizeof(uint32_t);
}

void RDMASocket::destroyConnection(rdma_cm_id *cmId)
{
    int peerId = cm2id[(uint64_t)cmId];
    peers[peerId].connected = false;
    peers[peerId].qp = nullptr;
    peers[peerId].cmId = nullptr;
    
    ibv_dereg_mr(peers[peerId].sendMR);
    ibv_dereg_mr(peers[peerId].recvMR);
    delete[] peers[peerId].sendRegion;
    delete[] peers[peerId].recvRegion;

    rdma_destroy_qp(cmId);
    rdma_destroy_id(cmId);
}

/* This function is expected to run as a single thread. */
void RDMASocket::listenSendCQ()
{
    ibv_cq *cq;
    ibv_wc wc;

    while (shouldRun) {
        expectZero(ibv_get_cq_event(compChannel[CQ_SEND], &cq, nullptr));
        ibv_ack_cq_events(cq, 1);
        expectZero(ibv_req_notify_cq(cq, 0));

        while (ibv_poll_cq(cq, 1, &wc))
            onSendCompletion(&wc);
    }
}

/* Show the QP status with the designated peer. */
void RDMASocket::verboseQP(int peerId)
{
    ibv_qp_attr attr;
    ibv_qp_init_attr init_attr;

    ibv_query_qp(peers[peerId].qp, &attr, IBV_QP_STATE, &init_attr);

#define CHECK(STATE)                                    \
        if (attr.qp_state == IBV_QPS_##STATE) {         \
            d_force("client %d QP: %s", peerId, #STATE);        \
            return;                                     \
        }
    CHECK(RESET);
    CHECK(INIT);
	CHECK(RTR);
	CHECK(RTS);
	CHECK(SQD);
	CHECK(SQE);
	CHECK(ERR);
	CHECK(UNKNOWN);
#undef CHECK
}

/* Check whether a peer is still alive (connected). */
bool RDMASocket::isPeerAlive(int peerId)
{
    return peers[peerId].connected;
}

/* Issue a send request to the designated peer, and return a unique task ID. */
uint32_t RDMASocket::postSend(int peerId, uint64_t length, int specialTaskId)
{
    if (!shouldRun) {
        d_err("send request after shouldRun=false is ignored");
        return 0;
    }
    if (!hashTable) {
        d_err("send request before registering a hash table is ignored");
        return 0;
    }

    ibv_send_wr wr, *badWr = nullptr;
    ibv_sge sge;
    uint32_t taskId = (specialTaskId < 0 ? hashTable->allocID() : specialTaskId);

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = ((uint64_t)(peers[peerId].sendRegion));
    sge.length = length;
    sge.lkey = mr->lkey;

    memset(&wr, 0, sizeof(ibv_send_wr));
    wr.wr_id = WRID(peerId, taskId);
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.imm_data = myNodeConf->id;
    wr.opcode = IBV_WR_SEND_WITH_IMM;
    wr.send_flags = IBV_SEND_SIGNALED;

    expectZero(ibv_post_send(peers[peerId].qp, &wr, &badWr));
    return taskId;
}

/* Issue a receive request to the designated peer. DOES NOT ALLOCATE HASHTABLE ID. */
void RDMASocket::postReceive(int peerId, uint64_t length, int specialTaskId)
{
    if (!shouldRun) {
        d_err("recv request after shouldRun=false is ignored");
        return;
    }
    if (!hashTable) {
        d_err("recv request before registering a hash table is ignored");
        return;
    }

    ibv_recv_wr wr, *badWr = nullptr;
    ibv_sge sge;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = (uint64_t)(peers[peerId].recvRegion);
    sge.length = length;
    sge.lkey = mr->lkey;

    memset(&wr, 0, sizeof(ibv_recv_wr));
    wr.wr_id = WRID(peerId, specialTaskId);
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.next = nullptr;

    expectZero(ibv_post_recv(peers[peerId].qp, &wr, &badWr));
}

/* Issue a write request to the designated peer, and return a unique task ID. */
uint32_t RDMASocket::postWrite(int peerId, uint64_t remoteDstShift, uint64_t localSrc,
                               uint64_t length, int imm, int specialTaskId)
{
    if (!shouldRun) {
        d_err("write request after shouldRun=false is ignored");
        return 0;
    }
    if (!hashTable) {
        d_err("write request before registering a hash table is ignored");
        return 0;
    }

    ibv_send_wr wr, *badWr = nullptr;
    ibv_sge sge;
    uint32_t taskId = (specialTaskId < 0 ? hashTable->allocID() : specialTaskId);

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = localSrc;
    sge.length = length;
    sge.lkey = mr->lkey;

    memset(&wr, 0, sizeof(ibv_send_wr));
    wr.wr_id = WRID(peerId, taskId);
    wr.sg_list = &sge;
    wr.num_sge = 1;
    if (imm == -1)
        wr.opcode = IBV_WR_RDMA_WRITE;
    else {
        wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        wr.imm_data = imm;
    }
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uint64_t)peers[peerId].peerMR.addr + remoteDstShift;
    wr.wr.rdma.rkey = peers[peerId].peerMR.rkey;

    expectZero(ibv_post_send(peers[peerId].qp, &wr, &badWr));
    return taskId;
}

/* Issue a read request from the designated peer, and return a unique task ID. */
uint32_t RDMASocket::postRead(int peerId, uint64_t remoteSrcShift, uint64_t localDst, 
                              uint64_t length, int specialTaskId)
{
    if (!shouldRun) {
        d_err("read request after shouldRun=false is ignored");
        return 0;
    }
    if (!hashTable) {
        d_err("read request before registering a hash table is ignored");
        return 0;
    }

    ibv_send_wr wr, *badWr = nullptr;
    ibv_sge sge;
    uint32_t taskId = (specialTaskId < 0 ? hashTable->allocID() : specialTaskId);

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = localDst;
    sge.length = length;
    sge.lkey = mr->lkey;

    memset(&wr, 0, sizeof(ibv_send_wr));
    wr.wr_id = WRID(peerId, taskId);
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uint64_t)peers[peerId].peerMR.addr + remoteSrcShift;
    wr.wr.rdma.rkey = peers[peerId].peerMR.rkey;

    expectZero(ibv_post_send(peers[peerId].qp, &wr, &badWr));
    return taskId;
}

/* Blockingly, statefully poll for next CQE in recv CQ. */
int RDMASocket::pollRecvCompletion(ibv_wc *wc)
{
    static ibv_cq *cq = nullptr;

    if (!cq) {
        expectZero(ibv_get_cq_event(compChannel[CQ_RECV], &cq, nullptr));
        ibv_ack_cq_events(cq, 1);
        expectZero(ibv_req_notify_cq(cq, 0));
    }

    int ret = ibv_poll_cq(cq, 1, wc);
    if (!ret)
        cq = nullptr;
    return ret;
}
