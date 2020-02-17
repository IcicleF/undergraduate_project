#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <debug.h>
#include <rdma.h>

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
        d_err("failed to create QP (with peer: %d)", peer->node_id);
        return -1;
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
        d_err("failed to modify QP to RTR (with peer: %d)", peer->node_id);
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
        d_err("failed to modify QP to RTS (with peer: %d)", peer->node_id);
        return -1;
    }
    return 0;
}
