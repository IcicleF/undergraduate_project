#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <config.hpp>
#include <debug.hpp>
#include <rdma.hpp>

RDMASocket::RDMASocket()
{
    if (cmdConf == nullptr || clusterConf == nullptr || memConf == nullptr || myNodeConf == nullptr) {
        d_err("all configurations should be initialized!");
        exit(-1);
    }

    if (createResources() < 0) {
        d_err("failed to create resources");
        exit(-1);
    }

    /* Listen first to make the port reusable */
    if (rdmaListen(cmdConf->tcpPort) < 0) {
        d_err("failed to listen incoming RDMA connections");
        exit(-1);
    }

    int nodeCount = clusterConf->getClusterSize();
    for (int i = 0; i < nodeCount; ++i) {
        NodeConfig nodeConf = (*clusterConf)[i];
        in_addr addr;
        addr.s_addr = nodeConf.ipAddr;
        d_warn("trying to connect peer %d (IP: %s) ...", i, inet_ntoa(addr));
        if (nodeConf.id >= myNodeConf->id)
            continue;
        if (nodeConf.type == NODE_CLI && myNodeConf->type == NODE_CLI)
            continue;
        if (rdmaConnect(nodeConf.id) < 0) {
            d_err("failed to connect with peer: %d", nodeConf.id);
            exit(-1);
        }
        else
            d_info("successfully connected with peer: %d", nodeConf.id);
    }


    d_info("successfully created RDMASocket!");
}

RDMASocket::~RDMASocket()
{
    disposeResources();
}

/*
 * Initialize necessary resources for all RDMA connections.
 * This function must be called before any other RDMA operations through this class.
 * 
 * Also, we recommend calling this function on ONLY ONE RDMASocket instance. 
 */
int RDMASocket::createResources()
{
    ibv_device **devList = nullptr;
    ibv_device *ibDev = nullptr;
    ibv_device_attr devAttr;
    int ret = 0;

    memset(&rs, 0, sizeof(rs));

    /* The do-while loop is executed only once and is used to avoid gotos */
    do {
        /* Find the desired IB device */
        int numDevices = 0;
        devList = ibv_get_device_list(&numDevices);
        if (devList == nullptr) {
            d_err("failed to get IB device list");
            ret = -1;
            break;
        }
        if (numDevices == 0) {
            d_err("cannot find any IB devices");
            ret = -1;
            break;
        }
        
        int i;
        for (i = 0; i < numDevices; ++i) {
            const char *devName = ibv_get_device_name(devList[i]);
            if (cmdConf->ibDeviceName == nullptr) {
                d_warn("IB device not specified, use the first one found: %s", devName);
                break;
            }
            if (strcmp(cmdConf->ibDeviceName, devName) == 0)
                break;
        }
        if (i >= numDevices) {
            d_err("IB device not found: %s", cmdConf->ibDeviceName);
            ret = -1;
            break;
        }
        
        ibDev = devList[i];
        
        rs.context = ibv_open_device(ibDev);
        if (rs.context == nullptr) {
            d_err("failed to open IB device: %s", ibv_get_device_name(ibDev));
            ret = -1;
            break;
        }
        
        /* Done with device list, free it */
        ibv_free_device_list(devList);
        devList = nullptr;
        ibDev = nullptr;
        
        /* Query device properties */
        if (ibv_query_device(rs.context, &devAttr) != 0) {
            d_err("failed to query device capabilities");
            ret = -1;
            break;
        }
        d_info("device max_cq = %d", devAttr.max_cq);
        d_info("device max_cqe = %d", devAttr.max_cqe);
        d_info("device max_raw_ethy_qp = %d", devAttr.max_raw_ethy_qp);

        if (ibv_query_port(rs.context, cmdConf->ibPort, &rs.portAttr) != 0) {
            d_err("failed to query IB port: %d", cmdConf->ibPort);
            ret = -1;
            break;
        }
        
        /* Allocate PD, CQ, MR */
        rs.pd = ibv_alloc_pd(rs.context);
        if (rs.pd == nullptr) {
            d_err("failed to allocate PD");
            ret = -1;
            break;
        }
        
        rs.cq = ibv_create_cq(rs.context, MAX_QP_DEPTH, nullptr, nullptr, 0);
        if (rs.cq == nullptr) {
            d_err("failed to create CQ (%u entries)", MAX_QP_DEPTH);
            ret = -1;
            break;
        }
        
        auto *memAddr = memConf->getMemory();
        auto memSize = memConf->getCapacity();
        int mrFlags = IBV_ACCESS_LOCAL_WRITE |
                      IBV_ACCESS_REMOTE_READ |
                      IBV_ACCESS_REMOTE_WRITE |
                      IBV_ACCESS_REMOTE_ATOMIC;
        rs.mr = ibv_reg_mr(rs.pd, memAddr, memSize, mrFlags);
        if (rs.mr == nullptr) {
            d_err("failed to register MR (addr=%p, size=%lu, flags=0x%x)", memAddr, memSize, mrFlags);
            ret = -1;
            break;
        }

        /* Initialize TCP sockets */
        for (i = 0; i < MAX_NODES; ++i)
            rs.peers[i].sock = -1;
    } while (0);

    if (ret != 0) {
        /* Error occured, cleanup in reversed order */
        if (rs.mr) {
            ibv_dereg_mr(rs.mr);
            rs.mr = nullptr;
        }
        if (rs.cq) {
            ibv_destroy_cq(rs.cq);
            rs.cq = nullptr;
        }
        if (rs.pd) {
            ibv_dealloc_pd(rs.pd);
            rs.pd = nullptr;
        }
        if (rs.context) {
            ibv_close_device(rs.context);
            rs.context = nullptr;
        }
        if (devList) {
            ibv_free_device_list(devList);
            devList = nullptr;
        }
    }
    return ret;
}

/*
 * Free all resources, including allocated memories, sockets and RDMA resources.
 * 
 * We recommend not to re-create the resources after disposing them.
 */
void RDMASocket::disposeResources()
{
    for (int i = 0; i < MAX_NODES; ++i) {
        auto *peer = &rs.peers[i];
        if (peer->sock > 0) {
            close(peer->sock);
            peer->sock = -1;
        }
        if (peer->qp) {
            ibv_destroy_qp(peer->qp);
            peer->qp = nullptr;
        }
    }

    if (rs.mr) {
        ibv_dereg_mr(rs.mr);
        rs.mr = nullptr;
    }
    if (rs.cq) {
        ibv_destroy_cq(rs.cq);
        rs.cq = nullptr;
    }
    if (rs.pd) {
        ibv_dealloc_pd(rs.pd);
        rs.pd = nullptr;
    }
    if (rs.context) {
        ibv_close_device(rs.context);
        rs.context = nullptr;
    }

    d_info("successfully destroyed RDMA resources");
}

/*
 * Actively establish an RDMA connection with the designated peer.
 */
int RDMASocket::rdmaConnect(int peerId)
{
    PeerInfo *peer = &rs.peers[peerId];
    peer->sock = socketConnect(peerId);
    if (peer->sock < 0) {
        d_err("failed to connect socket");
        return -1;
    }

    /* peer.qp, peer.cq are now valid */
    if (createQP(peer) < 0) {
        d_err("failed to create QP");
        return -1;
    }
    /* peer.connInfo is now valid */
    if (connectQP(peer) < 0) {
        d_err("failed to connect QP");
        return -1;
    }

    peer->sendBuf = memConf->getSendBuffer(peer->nodeId);
    peer->recvBuf = memConf->getReceiveBuffer(peer->nodeId);
    postReceive(peerId, 0);

    d_info("successfully built RDMA connection with peer: %d", peer->connInfo.nodeId);
    
    return 0;
}

/*
 * Show the QP status with the designated peer.
 */
void RDMASocket::verboseQP(int peerId)
{
    ibv_qp_attr attr;
    ibv_qp_init_attr init_attr;

    ibv_query_qp(rs.peers[peerId].qp, &attr, IBV_QP_STATE, &init_attr);

#define CHECK(STATE)                                    \
        if (attr.qp_state == IBV_QPS_##STATE) {         \
            d_force("client %d QP: %s", rs.peers[peerId].nodeId, #STATE);        \
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

/*
 * Issue a send request to the designated peer.
 */
int RDMASocket::postSend(int peerId, uint64_t localSrc, uint64_t length)
{
    ibv_send_wr sr;
    ibv_sge sge;
    ibv_send_wr *bad_wr = nullptr;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = localSrc;
    sge.length = length;
    sge.lkey = rs.mr->lkey;

    memset(&sr, 0, sizeof(ibv_send_wr));
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.imm_data = myNodeConf->id;
    sr.opcode = IBV_WR_SEND_WITH_IMM;
    sr.send_flags = IBV_SEND_SIGNALED;

    if (ibv_post_send(rs.peers[peerId].qp, &sr, &bad_wr) < 0) {
        d_err("failed to post RDMA send to peer: %d", peerId);
        return -1;
    }
    return 0;
}

/*
 * Prepare for incoming send requests from the designated peer.
 * Poll at CQ for the result.
 * Let length = 0 to allow any send lengths.
 * The incoming sends will be stored at the receive buffer for this peer (see code).
 */
int RDMASocket::postReceive(int peerId, uint64_t length)
{
    ibv_recv_wr rr;
    ibv_sge sge;
    ibv_recv_wr *bad_wr;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = (uint64_t)memConf->getReceiveBuffer(peerId);
    sge.length = length;
    sge.lkey = rs.mr->lkey;

    memset(&rr, 0, sizeof(ibv_recv_wr));
    rr.wr_id = 0;
    rr.sg_list = &sge;
    rr.num_sge = 1;

    if (ibv_post_recv(rs.peers[peerId].qp, &rr, &bad_wr) < 0) {
        d_err("failed to post RDMA recv");
        return -1;
    }
    return 0;
}

/*
 * Issue a write request to the designated peer.
 * Users should pass in a remote "address shift", since this function will add it to the base address.
 */
int RDMASocket::postWrite(int peerId, uint64_t remoteDstShift, uint64_t localSrc, uint64_t length, int imm)
{
    ibv_send_wr sr;
    ibv_sge sge;
    ibv_send_wr *bad_wr = nullptr;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = localSrc;
    sge.length = length;
    sge.lkey = rs.mr->lkey;

    memset(&sr, 0, sizeof(ibv_send_wr));
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    if (imm == -1)
        sr.opcode = IBV_WR_RDMA_WRITE;
    else {
        sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        sr.imm_data = imm;
    }
    sr.send_flags = IBV_SEND_SIGNALED;
    sr.wr.rdma.remote_addr = rs.peers[peerId].baseAddr + remoteDstShift;
    sr.wr.rdma.rkey = rs.peers[peerId].rkey;

    if (ibv_post_send(rs.peers[peerId].qp, &sr, &bad_wr) < 0) {
        d_err("failed to post RDMA write (%s)", strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Issue a read request from the designated peer.
 * Users should pass in a remote "address shift", since this function will add it
 * to the base address.
 */
int RDMASocket::postRead(int peerId, uint64_t remoteSrcShift, uint64_t localDst, uint64_t length)
{
    ibv_send_wr sr;
    ibv_sge sge;
    ibv_send_wr *bad_wr = nullptr;

    memset(&sge, 0, sizeof(ibv_sge));
    sge.addr = localDst;
    sge.length = length;
    sge.lkey = rs.mr->lkey;

    memset(&sr, 0, sizeof(ibv_send_wr));
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = IBV_WR_RDMA_READ;
    sr.send_flags = IBV_SEND_SIGNALED;
    sr.wr.rdma.remote_addr = rs.peers[peerId].baseAddr + remoteSrcShift;
    sr.wr.rdma.rkey = rs.peers[peerId].rkey;

    if (ibv_post_send(rs.peers[peerId].qp, &sr, &bad_wr) < 0) {
        d_err("failed to post RDMA read (%s)", strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Exchanges equal amount of data through a socket.
 */
int RDMASocket::socketExchangeData(int sock, int size, void *localData, void *remoteData)
{
    int readBytes = 0;
    int totalBytes = 0;
    int ret = write(sock, localData, size);

    if (ret < size) {
        d_err("failed to send data during sock_sync_data");
        return ret;
    }
    else
        ret = 0;
    
    while (ret == 0 && totalBytes < size)
    {
        uint8_t *startPos = reinterpret_cast<uint8_t *>(remoteData) + totalBytes;
        readBytes = read(sock, reinterpret_cast<void *>(startPos), size);
        if (readBytes > 0)
            totalBytes += readBytes;
        else
            ret = readBytes;
    }

    if (ret < size)
        d_err("failed to receive data during sock_sync_data");
    return ret;
}

/*
 * Actively establish TCP connection to the designated peer.
 * The peer's IP address is expected to be found from the cluster configuration.
 * 
 * RDMA resources are intact by this function.
 */
int RDMASocket::socketConnect(int peerId)
{
    auto peerConf = clusterConf->findConfById(peerId);

    sockaddr_in remote_addr;
    timeval timeout = {
        .tv_sec = 3,
        .tv_usec = 0
    };

    memset(&remote_addr, 0, sizeof(sockaddr_in));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = peerConf.ipAddr;
    remote_addr.sin_port = htons(cmdConf->tcpPort);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        d_err("failed to create socket");
        return -1;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeval)) < 0)
        d_warn("failed to set socket timeout");
    
    int retries;
    for (retries = 1; retries <= MAX_CONN_RETRIES; ++retries) {
        int ret = connect(sock, reinterpret_cast<sockaddr *>(&remote_addr), sizeof(sockaddr));
        if (ret < 0) {
            d_warn("cannot connect to remote %d (retry #%d)", peerId, retries);
            usleep(CONN_RETRY_INTERVAL);
        }
        else
            break;
    }
    if (retries > MAX_CONN_RETRIES) {
        d_err("failed to connect to remote: %d", peerId);
        return -1;
    }
    return sock;
}

/*
 * Initialize a new QP.
 */
int RDMASocket::createQP(PeerInfo *peer)
{
    /* Currently, connection to all peers share the same CQ */
    peer->cq = rs.cq;

    ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(ibv_qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = peer->cq;
    qp_init_attr.recv_cq = peer->cq;
    qp_init_attr.cap.max_send_wr = MAX_QP_DEPTH;
    qp_init_attr.cap.max_recv_wr = MAX_QP_DEPTH;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    peer->qp = ibv_create_qp(rs.pd, &qp_init_attr);
    if (peer->qp == nullptr) {
        d_err("failed to create QP (with peer: %d)", peer->connInfo.nodeId);
        return -1;
    }
    return 0;
}

int RDMASocket::connectQP(PeerInfo *peer)
{
    ConnInfo localConnInfo, remoteConnInfo;

    localConnInfo.baseAddr = (uint64_t)memConf->getMemory();
    localConnInfo.rkey = rs.mr->rkey;
    localConnInfo.qpn = peer->qp->qp_num;
    localConnInfo.lid = rs.portAttr.lid;
    localConnInfo.nodeId = myNodeConf->id;

    if (socketExchangeData(peer->sock, sizeof(ConnInfo), &localConnInfo, &remoteConnInfo) < 0) {
        d_err("failed to sync with remote");
        return -1;
    }
    memcpy(&peer->connInfo, &remoteConnInfo, sizeof(ConnInfo));

    d_info("successfully sync with peer: %d", remoteConnInfo.nodeId);

    if (createQP(peer) < 0 || modifyQPtoInit(peer) < 0 || modifyQPtoRTR(peer) < 0 ||
        modifyQPtoRTS(peer) < 0)
        return -1;

    d_info("successfully modified QP to Init,RTR,RTS (with peer: %d)", peer->connInfo.nodeId);
    return 0;
}

int RDMASocket::modifyQPtoInit(PeerInfo *peer)
{
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = cmdConf->ibPort;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                           IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_REMOTE_WRITE |
                           IBV_ACCESS_REMOTE_ATOMIC;
    
    int flags = IBV_QP_STATE |
                IBV_QP_PKEY_INDEX |
                IBV_QP_PORT |
                IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(peer->qp, &attr, flags) != 0) {
        d_err("failed to modify QP to init");
        return -1;
    }
    return 0;
}

int RDMASocket::modifyQPtoRTR(PeerInfo *peer)
{
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = peer->connInfo.qpn;
    attr.rq_psn = PSN_MAGIC;
    attr.max_dest_rd_atomic = MAX_DEST_RD_ATOMIC;
    attr.min_rnr_timer = 12;                        /* 640 us */
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = peer->connInfo.lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = cmdConf->ibPort;

    int flags = IBV_QP_STATE |
                IBV_QP_AV |
                IBV_QP_PATH_MTU |
                IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN |
                IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER;
    if (ibv_modify_qp(peer->qp, &attr, flags) != 0) {
        d_err("failed to modify QP to RTR (with peer: %d)", peer->connInfo.nodeId);
        return -1;
    }
    return 0;
}

int RDMASocket::modifyQPtoRTS(PeerInfo *peer)
{
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 12;              /* 16777.22 us */
    attr.retry_cnt = 7;             /* infinite times */
    attr.rnr_retry = 7;             /* infinite times */
    attr.sq_psn = PSN_MAGIC;
    attr.max_rd_atomic = 1;
    
    int flags = IBV_QP_STATE |
                IBV_QP_TIMEOUT |
                IBV_QP_RETRY_CNT |
                IBV_QP_RNR_RETRY |
                IBV_QP_SQ_PSN |
                IBV_QP_MAX_QP_RD_ATOMIC;
    if (ibv_modify_qp(peer->qp, &attr, flags) != 0) {
        d_err("failed to modify QP to RTS (with peer: %d)", peer->connInfo.nodeId);
        return -1;
    }
    return 0;
}

/*
 * Loop infinitely to passively accept incoming TCP connections.
 * For each TCP connection, exchange data and establish an RDMA connection.
 * 
 * This function is expected to be called by newing a thread.
 */
void RDMASocket::rdmaAccept(int sock)
{
    int acceptedPeers = 0;
    int expectedPeers = clusterConf->getClusterSize() - myNodeConf->id - 1;
    if (expectedPeers < 0) {
        d_warn("expected peers < 0, use +inf instead");
        expectedPeers = 99999;
    }
    if (expectedPeers == 0) {
        listenerQuit = true;
        return;
    }

    socklen_t socklen = sizeof(sockaddr);
    sockaddr_in remoteAddr;

    d_info("start RDMA listening...");
   
    while (isRunning.load()) {
        int fd = accept(sock, reinterpret_cast<sockaddr *>(&remoteAddr), &socklen);
        if (fd < 0) {
            d_err("error on accepting incoming TCP connections, stop");
            return;
        }

        d_warn("discovered new peer!!!");

        PeerInfo peer;
        memset(&peer, 0, sizeof(PeerInfo));
        peer.sock = fd;

        /* peer.qp, peer.cq are now valid */
        if (createQP(&peer) < 0) {
            d_err("failed to create QP, stop");
            return;
        }
        /* peer.connInfo is now valid */
        if (connectQP(&peer) < 0) {
            d_err("failed to connect QP, stop");
            return;
        }

        peer.sendBuf = memConf->getSendBuffer(peer.nodeId);
        peer.recvBuf = memConf->getReceiveBuffer(peer.nodeId);
        memcpy(&rs.peers[peer.nodeId], &peer, sizeof(PeerInfo));

        /* Prepare the first receive work request */
        postReceive(peer.nodeId, 0);
        
        if (++acceptedPeers >= expectedPeers) {
            d_info("%d peers connected as expected, stop", expectedPeers);
            listenerQuit = true;
            return;
        }
    }
}

int RDMASocket::rdmaListen(int port)
{
    sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(sockaddr_in));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(cmdConf->tcpPort);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        d_err("failed to create socket");
        return -1;
    }
    int on = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) < 0) {
        d_err("failed to set SO_REUSEADDR (%s)", strerror(errno));
        close(sock);
        return -1;
    }
    if (bind(sock, reinterpret_cast<sockaddr *>(&localAddr), sizeof(sockaddr)) < 0) {
        d_err("failed to bind socket (%s)", strerror(errno));
        close(sock);
        return -1;
    }

    listen(sock, MAX_QUEUED_CONNS);
    rdmaListener = std::thread(&RDMASocket::rdmaAccept, this, sock);
    rdmaListener.detach();

    return 0;
}

int RDMASocket::pollCompletion(ibv_wc *wc)
{
    int count = 0;

    while (1) {
        count = ibv_poll_cq(rs.cq, 1, wc);
        if (count < 0) {
            d_err("failed to poll from CQ (%s)", strerror(errno));
            return -1;
        }
        if (count > 0)
            break;
    }    
    
    if (wc->status != IBV_WC_SUCCESS) {
        d_err("failed wc (%d, %s), wr_id = %lu",
                (int)wc->status, ibv_wc_status_str(wc->status), wc->wr_id);
        return -1;
    }
    return count;
}

int RDMASocket::pollOnce(ibv_wc *wc)
{
    int count = ibv_poll_cq(rs.cq, 1, wc);
    
    if (count < 0) {
        d_err("failed to poll from CQ (%s)", strerror(errno));
        return -1;
    }
    if (count == 0)
        return 0;
    
    if (wc->status != IBV_WC_SUCCESS) {
        d_err("failed wc (%d, %s), wr_id = %lu",
                (int)wc->status, ibv_wc_status_str(wc->status), wc->wr_id);
        return -1;
    }
    return count;
}
