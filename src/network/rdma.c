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

/* This function MUST be called by pthread_create */
void *_sock_accept(void *_args)
{
    struct listener_args *args = (struct listener_args *)_args;
    struct sockaddr_in remote_addr;
    int fd;
    struct peer_conn_info peer;

    /* Detach from caller process and run independently */
    pthread_detach(pthread_self());

    while (atomic_load(&args->conf->running)) {
        fd = accept(args->sock, (struct sockaddr *)(&remote_addr), sizeof(struct sockaddr));
        if (fd < 0) {
            d_warn("cannot discover new incoming conecctions");
            break;
        }

        memset(&peer, 0, sizeof(struct peer_conn_info));
        peer.sock = fd;
        if (create_qp(args->rs, &peer) < 0) {
            d_err("failed to create QP, break");
            break;
        }
        if (connect_qp(args->rs, args->conf, &peer) < 0) {
            d_err("failed to connect QP, break");
            break;
        }

        d_info("successfully connected with peer: %d", peer.conn_data.node_id);

        memcpy(&args->rs->peers[peer.conn_data.node_id], &peer, sizeof(struct peer_conn_info));
        // TODO: RDMA Receive
    }
     
    pthread_exit(NULL);
}

int sock_listen(struct rdma_resource *rs, struct all_configs *conf)
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
            d_err("failed to set SO_REUSEADDR (errno: %d)", errno);
            ret = -1;
            break;
        }
        if (bind(sock, (struct sockaddr *)(&local_addr), sizeof(struct sockaddr)) < 0) {
            d_err("failed to bind socket (errno: %d)", errno);
            ret = -1;
            break;
        }

        listen(sock, MAX_QUEUED_CONNS);
        
        args.rs = rs;
        args.conf = conf;
        args.sock = sock;
        if (pthread_create(&listener, NULL, _sock_accept, &args) < 0) {
            d_err("failed to create listener thread");
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

int sock_connect(struct rdma_resource *rs, struct peer_conn_info *peer, struct fuse_cmd_config *conf)
{
    struct sockaddr_in remote_addr;
    struct timeval timeout = {
        .tv_sec = 3,
        .tv_usec = 0
    };
    int sock;
    int remote_id = peer->conn_data.node_id;
    struct node_config *remote_conf = find_conf_by_id(rs, remote_id);
    int retries;

    memset(&remote_addr, 0, sizeof(struct sockaddr_in));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = remote_conf->ip_addr;
    remote_addr.sin_port = htons(conf->tcp_port);

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
    if (rs == NULL)
        return 0;
    
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

/* Before calling this function, members {sock, qp} of argument `peer` must be properly set */
int connect_qp(struct rdma_resource *rs, struct all_configs *conf, struct peer_conn_info *peer)
{
    struct cm_conn_info local_conn_info;
    struct cm_conn_info remote_conn_info;
    int ret = 0;

    local_conn_info.addr = (uint64_t)conf->mem_conf->mem_loc;
    local_conn_info.rkey = rs->mr->rkey;
    local_conn_info.qpn = peer->qp->qp_num;
    local_conn_info.lid = rs->port_attr.lid;
    local_conn_info.node_id = conf->my_node_conf->id;

    /* The do-while loop is executed only once and is used to avoid gotos */
    do {
        ret = _sock_sync_data(peer->sock, sizeof(struct cm_conn_info), &local_conn_info, &remote_conn_info);
        if (ret < 0) {
            d_err("failed to sync with remote");
            break;
        }
        memcpy(&peer->conn_data, &remote_conn_info, sizeof(struct cm_conn_info));

        d_info("successfully sync with peer: %d", remote_conn_info.node_id);
        
        ret = modify_qp_to_init(peer->qp, conf->fuse_cmd_conf->ib_port);
        if (ret < 0)
            break;
        
        ret = modify_qp_to_rtr(peer->qp, conf->fuse_cmd_conf->ib_port, peer);
        if (ret < 0)
            break;
        
        ret = modify_qp_to_rts(peer->qp, peer);
        if (ret < 0)
            break;
        
        d_info("successfully modified QP to RTS (with peer: %d)", peer->conn_data.node_id);
    } while (0);

    return ret;
}

int _rdma_post_recv(struct rdma_resource *rs, struct peer_conn_info *peer, uint64_t src, uint64_t length)
{
    struct ibv_recv_wr rr;
    struct ibv_sge sge;
    struct ibv_recv_wr *bad_wr;

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
