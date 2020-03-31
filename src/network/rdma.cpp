#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/poll.h>

#include <config.hpp>
#include <debug.hpp>
#include <datablock.hpp>
#include <network/rdma.hpp>

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
    addr.sin_port = htons(cmdConf->tcpPort);

    for (int i = 0; i < MAX_NODES; ++i) {
        peers[i].cmId = nullptr;
        peers[i].qp = nullptr;
        peers[i].sendMR = nullptr;
        peers[i].recvMR = nullptr;
        peers[i].writeMR = nullptr;
        peers[i].readMR = nullptr;
        peers[i].sendRegion = nullptr;
        peers[i].recvRegion = nullptr;
        peers[i].connected = false;
    }

    expectNonZero(ec = rdma_create_event_channel());
    /* Make event channel non-blocking */
    int flags = fcntl(ec->fd, F_GETFL);
    if (fcntl(ec->fd, F_SETFL, flags | O_NONBLOCK) < 0)
        d_err("cannot change EC fd to non-blocking");
    else
        d_info("successfully changed EC fd to non-blocking");

    expectZero(rdma_create_id(ec, &listener, nullptr, RDMA_PS_TCP));
    expectZero(rdma_bind_addr(listener, reinterpret_cast<sockaddr *>(&addr)));
    expectZero(rdma_listen(listener, MAX_NODES));

    int port = ntohs(rdma_get_src_port(listener));
    expectTrue(port == cmdConf->tcpPort);
    d_info("listening on port: %d", port);

    char portStr[16];
    snprintf(portStr, 16, "%d", port);

    /* Connect to all peers with id < myId, or all other nodes if recovering */
    for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
        auto peerNode = (*clusterConf)[i];
        if (!cmdConf->recover && peerNode.id >= myNodeConf->id)
            continue;

        addrinfo *ai;
        getaddrinfo(peerNode.ibDevIPAddrStr.c_str(), portStr, nullptr, &ai);

        expectZero(rdma_create_id(ec, &peers[i].cmId, nullptr, RDMA_PS_TCP));
        cm2id[(uint64_t)peers[i].cmId] = i;
        expectZero(rdma_resolve_addr(peers[i].cmId, nullptr, ai->ai_addr, ADDR_RESOLVE_TIMEOUT));

        freeaddrinfo(ai);
    }

    ecPoller = std::thread(&RDMASocket::listenRDMAEvents, this);

    d_info("start waiting...");
    int expectedConns = clusterConf->getClusterSize() - 1;
    
    std::unique_lock<std::mutex> lock(stopSpinMutex);
    //ssmCondVar.wait(lock, [&, this](){ return this->incomingConns > expectedConns; });
    while (incomingConns < expectedConns) {
        ssmCondVar.wait(lock);
        d_info("ctor waken up, connections %d/%d", incomingConns, expectedConns);
    }

    initialized = true;
    writeLog.resize(WRITE_LOG_SIZE);
    d_info("successfully created RDMASocket!");
}

RDMASocket::~RDMASocket()
{
    stopListenerAndJoin();

    for (int i = 0; i < MAX_NODES; ++i)
        if (peers[i].cmId)
            destroyConnection(peers[i].cmId);
    for (int i = 0; i < MAX_CQS; ++i) {
        if (cq[i])
            ibv_destroy_cq(cq[i]);
        if (compChannel[i])
            ibv_destroy_comp_channel(compChannel[i]);
    }
    
    if (mr)
        ibv_dereg_mr(mr);
    if (pd)
        ibv_dealloc_pd(pd);
    if (listener)
        rdma_destroy_id(listener);
    if (ec)
        rdma_destroy_event_channel(ec);
}

/**
 * Stop RDMA event channel listener and joins the listener thread.
 * @note RDMA service are not disabled, but no new connections can be built.
 */
void RDMASocket::stopListenerAndJoin()
{
    if (std::this_thread::get_id() != mainThreadId) {
        d_err("cannot execute stopListenerAndJoin from non-main threads");
        return;
    }
    if (!shouldRun)
        return;

    shouldRun = false;
    /*
    if (ecPoller.joinable())
        ecPoller.join();
    */
    ecPoller.detach();

    //d_info("all joinable listener threads have joined");
    d_info("EC poller has detached");
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
    struct pollfd pfd = {
        .fd = ec->fd,
        .events = POLLIN,
        .revents = 0
    };
    while (shouldRun) {
        int ret = 0;
        do { 
            ret = poll(&pfd, 1, EC_POLL_TIMEOUT); 
        } while (shouldRun && ret == 0);
        if (!shouldRun)
            break;
        expectZero(rdma_get_cm_event(ec, &event));
        
        EventHandler handler = handlers[event->event];
        if (handler)
            (this->*handler)(event);
        else
            d_warn("RDMA CM event type %d not handled", (int)event->event);
        rdma_ack_cm_event(event);
    }

    d_info("RDMASocket has stopped listening EC events.");
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
    RDMAConnection *peer = peers + cm2id[(uint64_t)event->id];
    expectTrue(peer == reinterpret_cast<RDMAConnection *>(event->id->context));

    /* Send MR to peer */
    auto *mrMsg = reinterpret_cast<Message *>(peer->sendRegion);
    mrMsg->type = Message::MESG_REMOTE_MR;
    memcpy(&mrMsg->data.mr, mr, sizeof(ibv_mr));
    postSend(peer->peerId, sizeof(Message));

    /*
     * Post another recv to ensure that there is always an outstanding RDMA recv.
     * Because there is already an RDMA recv, this recv shouldn't receive the remote MR.
     */
    postReceive(peer->peerId, sizeof(Message));

    /* If I am already initialized, I won't race for this MR with RPC listener process. */
    if (initialized)
        return;

    /*
     * Immediately (and blockingly) poll this RDMA recv.
     * Because the connection is already established, we expect not blocking very long.
     */
    ibv_wc wc[2];
    expectPositive(pollRecvCompletion(wc));
    
    if (wc->status != IBV_WC_SUCCESS) {
        d_err("wc->status = %d", (int)wc->status);
        exit(-1);
    }
    if (WRID_TASK(wc->wr_id) == SP_REMOTE_MR_RECV) {
        auto *msg = reinterpret_cast<Message *>(peer->recvRegion);
        if (msg->type == Message::MESG_REMOTE_MR) {
            memcpy(&peer->peerMR, &msg->data.mr, sizeof(ibv_mr));
            peer->connected = true;
            d_info("successfully connected with peer: %d (%p)", peer->peerId, (void *)peer->peerMR.addr);

            /* Safe because here, the RDMASocket is definitely not completely initialized */
            ++incomingConns;
            {
                std::unique_lock<std::mutex> lock(stopSpinMutex);
                ssmCondVar.notify_one();
            }
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
    int peerId = cm2id[(uint64_t)event->id];
    d_warn("peer %d has disconnected!", peerId);

    destroyConnection(event->id);
    ++degraded;
}

void RDMASocket::buildResources(ibv_context *ctx)
{
    if (this->ctx) {
        if (this->ctx != ctx)
            d_err("more than one context...");
        return;
    }

    d_info("resource bound to device: %s", ctx->device->name);

    this->ctx = ctx;
    expectNonZero(pd = ibv_alloc_pd(this->ctx));
    for (int i = 0; i < MAX_CQS; ++i) {
        expectNonZero(compChannel[i] = ibv_create_comp_channel(this->ctx));
        expectNonZero(cq[i] = ibv_create_cq(this->ctx, MAX_QP_DEPTH, nullptr, compChannel[i], 0));
        // expectZero(ibv_req_notify_cq(cq[i], 0));
    }
    
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
    peer->writeRegion = new uint8_t[Block4K::capacity];
    peer->readRegion = new uint8_t[Block4K::capacity];

    expectNonZero(peer->sendMR = ibv_reg_mr(pd, peer->sendRegion, RDMA_BUF_SIZE, 0));
    expectNonZero(peer->recvMR = ibv_reg_mr(pd, peer->recvRegion, RDMA_BUF_SIZE, IBV_ACCESS_LOCAL_WRITE));
    expectNonZero(peer->writeMR = ibv_reg_mr(pd, peer->writeRegion, Block4K::capacity, 0));
    expectNonZero(peer->readMR = ibv_reg_mr(pd, peer->readRegion, Block4K::capacity, IBV_ACCESS_LOCAL_WRITE));

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
    ibv_dereg_mr(peers[peerId].writeMR);
    ibv_dereg_mr(peers[peerId].readMR);

    delete[] peers[peerId].sendRegion;
    delete[] peers[peerId].recvRegion;
    delete[] peers[peerId].writeRegion;
    delete[] peers[peerId].readRegion;

    cm2id.erase((uint64_t)cmId);

    rdma_destroy_qp(cmId);
    rdma_destroy_id(cmId);
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
    if (peerId == myNodeConf->id)
        return true;
    return peers[peerId].connected;
}

/* Issue a send request to the designated peer, and return a unique task ID. */
void RDMASocket::postSend(int peerId, uint64_t length)
{
    if (!shouldRun) {
        d_err("send request after shouldRun=false is ignored");
        return;
    }

    ibv_send_wr wr, *badWr = nullptr;
    ibv_sge sge;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = ((uint64_t)(peers[peerId].sendRegion));
    sge.length = length;
    sge.lkey = peers[peerId].sendMR->lkey;

    memset(&wr, 0, sizeof(ibv_send_wr));
    wr.wr_id = WRID(peerId, 0);
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.imm_data = myNodeConf->id;
    wr.opcode = IBV_WR_SEND_WITH_IMM;
    //wr.send_flags = IBV_SEND_INLINE;

    expectZero(ibv_post_send(peers[peerId].qp, &wr, &badWr));
}

/* Issue a receive request to the designated peer. DOES NOT ALLOCATE HASHTABLE ID. */
void RDMASocket::postReceive(int peerId, uint64_t length, int specialTaskId)
{
    if (!shouldRun) {
        d_err("recv request after shouldRun=false is ignored");
        return;
    }

    ibv_recv_wr wr, *badWr = nullptr;
    ibv_sge sge;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = (uint64_t)(peers[peerId].recvRegion);
    sge.length = length;
    sge.lkey = peers[peerId].recvMR->lkey;

    memset(&wr, 0, sizeof(ibv_recv_wr));
    wr.wr_id = WRID(peerId, specialTaskId);
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.next = nullptr;

    expectZero(ibv_post_recv(peers[peerId].qp, &wr, &badWr));
}

/* Issue a write request to the designated peer, and return a unique task ID. */
void RDMASocket::postWrite(int peerId, uint64_t remoteDstShift, uint64_t localSrc,
                           uint64_t length, int imm)
{
    if (!shouldRun) {
        d_err("write request after shouldRun=false is ignored");
        return;
    }

    ibv_send_wr wr, *badWr = nullptr;
    ibv_sge sge;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = localSrc;
    sge.length = length;
    sge.lkey = peers[peerId].writeMR->lkey;

    memset(&wr, 0, sizeof(ibv_send_wr));
    wr.wr_id = WRID(peerId, 0);
    wr.sg_list = &sge;
    wr.num_sge = 1;
    if (imm == -1)
        wr.opcode = IBV_WR_RDMA_WRITE;
    else {
        wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        wr.imm_data = imm;
    }
    //wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uint64_t)peers[peerId].peerMR.addr + remoteDstShift;
    wr.wr.rdma.rkey = peers[peerId].peerMR.rkey;

    expectZero(ibv_post_send(peers[peerId].qp, &wr, &badWr));
}

/* Issue a read request from the designated peer, and return a unique task ID. */
void RDMASocket::postRead(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length, uint32_t taskId)
{
    if (!shouldRun) {
        d_err("read request after shouldRun=false is ignored");
        return;
    }

    ibv_send_wr wr, *badWr = nullptr;
    ibv_sge sge;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = localDst;
    sge.length = length;
    sge.lkey = peers[peerId].readMR->lkey;

    memset(&wr, 0, sizeof(ibv_send_wr));
    wr.wr_id = WRID(peerId, taskId);
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uint64_t)peers[peerId].peerMR.addr + remoteSrcShift;
    wr.wr.rdma.rkey = peers[peerId].peerMR.rkey;

    expectZero(ibv_post_send(peers[peerId].qp, &wr, &badWr));
}

/* Poll for next CQE in send CQ (RDMA read). */
int RDMASocket::pollSendCompletion(ibv_wc *wc)
{
    while (shouldRun) {
        int ret = ibv_poll_cq(cq[CQ_SEND], 1, wc);
        if (ret)
            return ret;
    }
    return 0;
}

/**
 * Poll for next CQE in recv CQ (RDMA recv).
 * This function automatically processes and skips CQEs caused by remote write-with-imm's
 * or reconnections.
 */
int RDMASocket::pollRecvCompletion(ibv_wc *wc)
{
    while (shouldRun) {
        int ret = ibv_poll_cq(cq[CQ_RECV], 1, wc);
        if (ret) {
            if (unlikely(WRID_TASK(wc->wr_id) == SP_REMOTE_MR_RECV)) {
                /* Remote MR from a reconnected peer received. No need to repost recv. */
                auto *peer = peers + WRID_PEER(wc->wr_id);
                auto *msg = reinterpret_cast<Message *>(peer->recvRegion);
                if (msg->type == Message::MESG_REMOTE_MR) {
                    memcpy(&peer->peerMR, &msg->data.mr, sizeof(ibv_mr));
                    peer->connected = true;
                    d_info("successfully reconnected with peer: %d (%p)", peer->peerId, (void *)peer->peerMR.addr);
                }
                else
                    d_err("RDMA recv intended for MR received some other thing");
                continue;
            }
            if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
                /* Transparently processes it, post another recv and retry. */
                processRecvWriteWithImm(wc);
                postReceive(WRID_PEER(wc->wr_id), sizeof(Message));
                continue;
            }
            return ret;
        }
    }
    return 0;
}

/** Process RDMA write-with-imm's to this node */
void RDMASocket::processRecvWriteWithImm(ibv_wc *wc)
{
    if (degraded) {
        uint64_t blkno = wc->imm_data;
        d_warn("EC group is degraded, record write to block %lu", blkno);
        if (writeLog.size() == writeLog.capacity()) {
            d_err("write log is already full!!  this write will not be logged.");
            return;
        }
        writeLog.push_back(blkno);
    }
}
