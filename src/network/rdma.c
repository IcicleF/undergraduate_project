#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <config.h>
#include <debug.h>
#include <rdma.h>

struct listener_args
{
    struct rdma_resource *rs;
    struct all_configs *conf;
    int sock;
};

/* Before calling this function, peer->conn_data.node_id must be properly set */
int sock_connect(struct rdma_resource *rs, struct all_configs *conf, struct peer_conn_info *peer)
{
    struct sockaddr_in remote_addr;
    struct timeval timeout = {
        .tv_sec = 3,
        .tv_usec = 0
    };
    int sock;
    int remote_id = peer->conn_data.node_id;
    struct node_config *remote_conf = find_conf_by_id(conf->cluster_conf, remote_id);
    int retries;

    memset(&remote_addr, 0, sizeof(struct sockaddr_in));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = remote_conf->ip_addr;
    remote_addr.sin_port = htons(conf->fuse_cmd_conf->tcp_port);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        d_err("failed to create socket");
        return -1;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval)) < 0)
        d_warn("failed to set socket timeout");
    
    for (retries = 1; retries <= MAX_CONN_RETRIES; ++retries) {
        int ret = connect(sock, (struct sockaddr *)(&remote_addr), sizeof(struct sockaddr));
        if (ret < 0) {
            d_warn("cannot connect to remote %d (retry #%d)", remote_id, retries);
            usleep(CONN_RETRY_INTERVAL);
        }
        else
            break;
    }
    if (retries > MAX_CONN_RETRIES) {
        d_err("failed to connect to remote: %d", remote_id);
        return -1;
    }

    return sock;
}

int sock_sync_data(int sock, int size, void *local_data, void *remote_data)
{
    int ret;
    int read_bytes = 0;
    int total_read_bytes = 0;
    ret = write(sock, local_data, size);

    if (ret < size) {
        d_err("failed to send data during sock_sync_data");
        return ret;
    }
    else
        ret = 0;
    
    while (ret == 0 && total_read_bytes < size)
    {
        read_bytes = read(sock, remote_data + total_read_bytes, size);
        if (read_bytes > 0)
            total_read_bytes += read_bytes;
        else
            ret = read_bytes;
    }

    if (ret < size)
        d_err("failed to receive data during sock_sync_data");
    return ret;
}


int create_resources(struct rdma_resource *rs, struct all_configs *conf)
{
    struct ibv_device **dev_list = NULL;
    struct ibv_device *ib_dev = NULL;
    struct ibv_device_attr dev_attr;
    int num_devices;
    int mr_flags;
    void *mem_loc;
    uint64_t mem_size;
    
    int i;
    int ret = 0;

    if (rs == NULL || conf == NULL)
        return -1;

    memset(rs, 0, sizeof(struct rdma_resource));

    /* The do-while loop is executed only once and is used to avoid gotos */
    do {
        /* Find the desired IB device */
        dev_list = ibv_get_device_list(&num_devices);
        if (dev_list == NULL) {
            d_err("failed to get IB device list");
            ret = -1;
            break;
        }
        if (num_devices == 0) {
            d_err("cannot find any IB devices");
            ret = -1;
            break;
        }
        
        for (i = 0; i < num_devices; ++i) {
            const char *dev_name = ibv_get_device_name(dev_list[i]);
            if (conf->fuse_cmd_conf->ib_dev_name == NULL) {
                d_warn("IB device not specified, use the first one found: %s", dev_name);
                break;
            }
            if (strcmp(conf->fuse_cmd_conf->ib_dev_name, dev_name) == 0)
                break;
        }
        if (i >= num_devices) {
            d_err("IB device not found: %s", conf->fuse_cmd_conf->ib_dev_name);
            ret = -1;
            break;
        }
        
        ib_dev = dev_list[i];
        
        rs->context = ibv_open_device(ib_dev);
        if (rs->context == NULL) {
            d_err("failed to open IB device: %s", ibv_get_device_name(ib_dev));
            ret = -1;
            break;
        }
        
        /* Done with device list, free it */
        ibv_free_device_list(dev_list);
        dev_list = NULL;
        ib_dev = NULL;
        
        /* Query device properties */
        if (ibv_query_device(rs->context, &dev_attr) != 0) {
            d_err("failed to query device capabilities");
            ret = -1;
            break;
        }
        d_info("device max_cq = %d", dev_attr.max_cq);
        d_info("device max_cqe = %d", dev_attr.max_cqe);
        d_info("device max_raw_ethy_qp = %d", dev_attr.max_raw_ethy_qp);

        if (ibv_query_port(rs->context, conf->fuse_cmd_conf->ib_port, &rs->port_attr) != 0) {
            d_err("failed to query port: %d", conf->fuse_cmd_conf->ib_port);
            ret = -1;
            break;
        }
        
        /* Allocate PD, CQ, MR */
        rs->pd = ibv_alloc_pd(rs->context);
        if (rs->pd == NULL) {
            d_err("failed to allocate PD");
            ret = -1;
            break;
        }
        
        rs->cq = ibv_create_cq(rs->context, MAX_QP_DEPTH, NULL, NULL, 0);
        if (rs->cq == NULL) {
            d_err("failed to create CQ (%u entries)", MAX_QP_DEPTH);
            ret = -1;
            break;
        }
        
        mem_loc = conf->mem_conf->mem_loc;
        mem_size = conf->mem_conf->mem_size;
        mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
        rs->mr = ibv_reg_mr(rs->pd, mem_loc, mem_size, mr_flags);
        if (rs->mr == NULL) {
            d_err("failed to register MR (addr=%p, size=%lu, flags=0x%x)", mem_loc, mem_size, mr_flags);
            ret = -1;
            break;
        }

        /* Initialize TCP sockets */
        for (i = 0; i < MAX_PEERS; ++i)
            rs->peers[i].sock = -1;
    } while (0);

    if (ret != 0) {
        /* Error occured, cleanup in reversed order */
        if (rs->mr) {
            ibv_dereg_mr(rs->mr);
            rs->mr = NULL;
        }
        if (rs->cq) {
            ibv_destroy_cq(rs->cq);
            rs->cq = NULL;
        }
        if (rs->pd) {
            ibv_dealloc_pd(rs->pd);
            rs->pd = NULL;
        }
        if (rs->context) {
            ibv_close_device(rs->context);
            rs->context = NULL;
        }
        if (dev_list) {
            ibv_free_device_list(dev_list);
            dev_list = NULL;
        }
    }
    return ret;
}

int destroy_resources(struct rdma_resource *rs)
{
    int i;

    if (rs == NULL)
        return 0;
    
    /* Close sockets, free RDMA recv buffers */
    for (i = 0; i < MAX_NODES; ++i) {
        struct peer_conn_info *peer = &rs->peers[i];
        if (peer->sock > 0) {
            close(peer->sock);
            peer->sock = -1;
        }
        if (peer->buf != NULL) {
            free(peer->buf);
            peer->buf = NULL;
        }
    }

    /* Destroy all QPs */
    destroy_qp(rs);
    
    /* Destroy MR, CQ, PD & Context */
    if (rs->mr) {
        ibv_dereg_mr(rs->mr);
        rs->mr = NULL;
    }
    if (rs->cq) {
        ibv_destroy_cq(rs->cq);
        rs->cq = NULL;
    }
    if (rs->pd) {
        ibv_dealloc_pd(rs->pd);
        rs->pd = NULL;
    }
    if (rs->context) {
        ibv_close_device(rs->context);
        rs->context = NULL;
    }

    d_info("successfully destroyed RDMA resources");

    return 0;
}

int create_qp(struct rdma_resource *rs, struct peer_conn_info *peer)
{
    struct ibv_qp_init_attr qp_init_attr;

    /* Currently connection to all peers share the same CQ */
    peer->cq = rs->cq;

    memset(&qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = peer->cq;
    qp_init_attr.recv_cq = peer->cq;
    qp_init_attr.cap.max_send_wr = MAX_QP_DEPTH;
    qp_init_attr.cap.max_recv_wr = MAX_QP_DEPTH;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    peer->qp = ibv_create_qp(rs->pd, &qp_init_attr);
    if (peer->qp == NULL) {
        d_err("failed to create QP (with peer: %d)", peer->conn_data.node_id);
        return -1;
    }
    return 0;
}

int destroy_qp(struct rdma_resource *rs)
{
    int i;

    if (rs == NULL)
        return 0;
    
    for (i = 0; i < MAX_NODES; ++i) {
        struct peer_conn_info *peer = &rs->peers[i];
        if (peer->qp != NULL) {
            ibv_destroy_qp(peer->qp);
            peer->qp = NULL;
        }
    }
    return 0;
}

int modify_qp_to_init(struct ibv_qp *qp, int ib_port)
{
    struct ibv_qp_attr attr;
    int flags;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = ib_port;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
    flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

    if (ibv_modify_qp(qp, &attr, flags) != 0) {
        d_err("failed to modify QP to init");
        return -1;
    }
    return 0;
}

int modify_qp_to_rtr(struct ibv_qp *qp, int ib_port, struct peer_conn_info *peer)
{
    struct ibv_qp_attr attr;
    int flags;
    
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = peer->conn_data.qpn;
    attr.rq_psn = PSN_MAGIC;
    attr.max_dest_rd_atomic = MAX_DEST_RD_ATOMIC;
    attr.min_rnr_timer = 12;        /* 640 us */
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = peer->conn_data.lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = ib_port;

    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    if (ibv_modify_qp(qp, &attr, flags) != 0) {
        d_err("failed to modify QP to RTR (with peer: %d)", peer->conn_data.node_id);
        return -1;
    }
    return 0;
}

int modify_qp_to_rts(struct ibv_qp *qp, struct peer_conn_info *peer)
{
    struct ibv_qp_attr attr;
    int flags;
    
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 12;              /* 16777.22 us */
    attr.retry_cnt = 7;             /* infinite times */
    attr.rnr_retry = 7;             /* infinite times */
    attr.sq_psn = PSN_MAGIC;
    attr.max_rd_atomic = 1;
    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    
    if (ibv_modify_qp(qp, &attr, flags) != 0) {
        d_err("failed to modify QP to RTS (with peer: %d)", peer->conn_data.node_id);
        return -1;
    }
    return 0;
}

/* Before calling this function, peer->sock must be properly set */
int connect_qp(struct rdma_resource *rs, struct all_configs *conf, struct peer_conn_info *peer)
{
    struct cm_conn_info local_conn_info;
    struct cm_conn_info remote_conn_info;

    local_conn_info.addr = (uint64_t)conf->mem_conf->mem_loc;
    local_conn_info.rkey = rs->mr->rkey;
    local_conn_info.qpn = peer->qp->qp_num;
    local_conn_info.lid = rs->port_attr.lid;
    local_conn_info.node_id = my_node_conf->id;

    if (sock_sync_data(peer->sock, sizeof(struct cm_conn_info), &local_conn_info, &remote_conn_info) < 0) {
        d_err("failed to sync with remote");
        return -1;
    }
    memcpy(&peer->conn_data, &remote_conn_info, sizeof(struct cm_conn_info));

    d_info("successfully sync with peer: %d", remote_conn_info.node_id);

    if (create_qp(rs, peer) < 0
        || modify_qp_to_init(peer->qp, conf->fuse_cmd_conf->ib_port) < 0
        || modify_qp_to_rtr(peer->qp, conf->fuse_cmd_conf->ib_port, peer) < 0
        || modify_qp_to_rts(peer->qp, peer) < 0)
        return -1;

    d_info("successfully modified QP to RTS (with peer: %d)", peer->conn_data.node_id);
    return 0;
}

void verbose_qp(struct peer_conn_info *peer)
{
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    
    ibv_query_qp(peer->qp, &attr, IBV_QP_STATE, &init_attr);

#define CHECK(STATE)                                    \
        if (attr.qp_state == IBV_QPS_##STATE) {         \
            d_force("client %d QP: %s", peer->conn_data.node_id, #STATE);        \
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


/* This function MUST be called by pthread_create */
void *rdma_accept(void *_args)
{
    struct listener_args *args = (struct listener_args *)_args;
    struct sockaddr_in remote_addr;
    int fd;
    struct peer_conn_info peer;
    struct peer_conn_info *target_peer;
    socklen_t socklen = sizeof(struct sockaddr);
    int accepted_peers = 0;

    /* Detach from caller process and run independently */
    pthread_detach(pthread_self());

    while (atomic_load(&running)) {
        fd = accept(args->sock, (struct sockaddr *)(&remote_addr), &socklen);
        if (fd < 0) {
            d_warn("cannot discover new incoming conecctions");
            break;
        }

        memset(&peer, 0, sizeof(struct peer_conn_info));
        peer.sock = fd;
        peer.buf = malloc(RDMA_RECV_BUF_SIZE);
        memset(peer.buf, 0, RDMA_RECV_BUF_SIZE);

        if (create_qp(args->rs, &peer) < 0) {
            d_err("failed to create QP, break");
            break;
        }
        if (connect_qp(args->rs, args->conf, &peer) < 0) {
            d_err("failed to connect QP, break");
            break;
        }

        d_info("successfully accepted connection from peer: %d", peer.conn_data.node_id);

        target_peer = &args->rs->peers[peer.conn_data.node_id];
        memcpy(target_peer, &peer, sizeof(struct peer_conn_info));
        rdma_post_recv(args->rs, target_peer, (uint64_t)peer.buf, 0);

        ++accepted_peers;

        /* Check if all peers have connected (assuming node ID starts from 0) */
        if (my_node_conf->id + accepted_peers + 1 == args->conf->cluster_conf->node_count) {
            d_info("all peers AFTER SELF have connected, break");
            break;
        }
    }

    pthread_exit(NULL);
}

int rdma_listen(struct rdma_resource *rs, struct all_configs *conf)
{
    struct sockaddr_in local_addr;
    int ret = 0;
    int sock;
    int on = 1;
    pthread_t listener;
    struct listener_args args;

    memset(&local_addr, 0, sizeof(struct sockaddr_in));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(conf->fuse_cmd_conf->tcp_port);

    /* The do-while loop is executed only once and is used to avoid gotos */
    do {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            d_err("failed to create socket");
            ret = -1;
            break;
        }
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) < 0) {
            d_err("failed to set SO_REUSEADDR (%s)", strerror(errno));
            ret = -1;
            break;
        }
        if (bind(sock, (struct sockaddr *)(&local_addr), sizeof(struct sockaddr)) < 0) {
            d_err("failed to bind socket (%s)", strerror(errno));
            ret = -1;
            break;
        }

        listen(sock, MAX_QUEUED_CONNS);
        
        args.rs = rs;
        args.conf = conf;
        args.sock = sock;
        if (pthread_create(&listener, NULL, rdma_accept, &args) < 0) {
            d_err("failed to create listener thread");
            ret = -1;
            break;
        }
    } while (0);

    if (ret != 0) {
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
    }
    return ret;
}

int rdma_connect(struct rdma_resource *rs, struct all_configs *conf, int peer_id)
{
    if (rs == NULL || conf == NULL)
        return -1;

    struct peer_conn_info *peer = &rs->peers[peer_id];
    peer->conn_data.node_id = peer_id;

    peer->sock = sock_connect(rs, conf, peer);
    if (peer->sock < 0) {
        d_err("failed to connect socket");
        return -1;
    }
    if (connect_qp(rs, conf, peer) < 0) {
        d_err("failed to connect QP");
        return -1;
    } 

    d_info("successfully built RDMA connection with peer: %d", peer->conn_data.node_id);
    return 0;
}

int rdma_post_recv(struct rdma_resource *rs, struct peer_conn_info *peer, uint64_t src, uint64_t length)
{
    struct ibv_recv_wr rr;
    struct ibv_sge sge;
    struct ibv_recv_wr *bad_wr;

    if (rs == NULL || peer == NULL)
        return -1;

    memset(&sge, 0, sizeof(struct ibv_sge));
    sge.addr = src;
    sge.length = length;
    sge.lkey = rs->mr->lkey;

    memset(&rr, 0, sizeof(struct ibv_recv_wr));
    rr.wr_id = 0;
    rr.sg_list = &sge;
    rr.num_sge = 1;

    if (ibv_post_recv(peer->qp, &rr, &bad_wr) < 0) {
        d_err("failed to post RDMA recv");
        return -1;
    }
    return 0;
}

int rdma_post_send(struct rdma_resource *rs, struct peer_conn_info *peer, uint64_t src, uint64_t length)
{
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;

    if (rs == NULL || peer == NULL)
        return -1;

    memset(&sge, 0, sizeof(struct ibv_sge));
    sge.addr = src;
    sge.length = length;
    sge.lkey = rs->mr->lkey;

    memset(&sr, 0, sizeof(struct ibv_send_wr));
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.imm_data = my_node_conf->id;
    sr.opcode = IBV_WR_SEND_WITH_IMM;
    sr.send_flags = IBV_SEND_SIGNALED;

    if (ibv_post_send(peer->qp, &sr, &bad_wr) < 0) {
        d_err("failed to post RDMA send");
        return -1;
    }
    return 0;
}

int rdma_post_read(struct rdma_resource *rs, struct peer_conn_info *peer, uint64_t dest, uint64_t src, uint64_t length)
{
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;

    if (rs == NULL || peer == NULL)
        return -1;
    
    memset(&sge, 0, sizeof(struct ibv_sge));
    sge.addr = dest;
    sge.length = length;
    sge.lkey = rs->mr->lkey;

    memset(&sr, 0, sizeof(struct ibv_send_wr));
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = IBV_WR_RDMA_READ;
    sr.send_flags = IBV_SEND_SIGNALED;
    sr.wr.rdma.remote_addr = peer->conn_data.addr + src;    /* Offset to remote base addr */
    sr.wr.rdma.rkey = peer->conn_data.rkey;

    if (ibv_post_send(peer->qp, &sr, &bad_wr) < 0) {
        d_err("failed to post RDMA read (%s)", strerror(errno));
        return -1;
    }

    return 0;
}

int rdma_post_write(struct rdma_resource *rs, struct peer_conn_info *peer, uint64_t dest, uint64_t src, uint64_t length, int imm)
{
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;

    if (rs == NULL || peer == NULL)
        return -1;
    
    memset(&sge, 0, sizeof(struct ibv_sge));
    sge.addr = dest;
    sge.length = length;
    sge.lkey = rs->mr->lkey;

    memset(&sr, 0, sizeof(struct ibv_send_wr));
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
    sr.wr.rdma.remote_addr = peer->conn_data.addr + src;    /* Offset to remote base addr */
    sr.wr.rdma.rkey = peer->conn_data.rkey;

    if (ibv_post_send(peer->qp, &sr, &bad_wr) < 0) {
        d_err("failed to post RDMA write (%s)", strerror(errno));
        return -1;
    }

    return 0;
}


int try_poll_cq_once(struct rdma_resource *rs, struct ibv_wc *wc)
{
    int count = ibv_poll_cq(rs->cq, 1, wc);
    
    if (count < 0) {
        d_err("failed to poll from CQ (%s)", strerror(errno));
        return -1;
    }
    if (count == 0)
        return 0;
    
    if (wc->status != IBV_WC_SUCCESS) {
        d_err("failed wc (%d, %s), wr_id = %d",
                (int)wc->status, ibv_wc_status_str(wc->status), wc->wr_id);
        return -1;
    }
    return count;
}

int poll_cq_once(struct rdma_resource *rs, struct ibv_wc *wc)
{
    int count = 0;

    while (1) {
        count = ibv_poll_cq(rs->cq, 1, wc);
        if (count < 0) {
            d_err("failed to poll from CQ (%s)", strerror(errno));
            return -1;
        }
        if (count > 0)
            break;
    }    
    
    if (wc->status != IBV_WC_SUCCESS) {
        d_err("failed wc (%d, %s), wr_id = %d",
                (int)wc->status, ibv_wc_status_str(wc->status), wc->wr_id);
        return -1;
    }
    return count;
}