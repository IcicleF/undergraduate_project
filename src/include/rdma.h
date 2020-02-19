#if !defined(RDMA_H)
#define RDMA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "config.h"

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htonll(x) bswap_64((uint64_t)(x))
#define ntohll(x) bswap_64((uint64_t)(x))
#elif __BYTE_ORDER == __BIG_ENDIAN
#define htonll(x) ((uint64_t)(x))
#define ntohll(x) ((uint64_t)(x))
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

/* Structure to exchange data which is needed to connect the QPs */
struct cm_conn_info
{
    uint64_t addr;                          /* Buffer address */
    uint32_t rkey;                          /* Remote key */
    uint32_t qpn;                           /* QP number */
    uint16_t lid;                           /* LID of the IB port */
    uint32_t node_id;                       /* Remote Node ID */
} __attribute__((packed));

/* Store the RDMA connection data with a peer */
struct peer_conn_info
{
    struct ibv_qp *qp;                      /* QP handle */
    struct ibv_cq *cq;                      /* CQ handle */

    struct cm_conn_info conn_data;
    int sock;                               /* TCP socket */
};

/* Store all necessary resources for RDMA connection with other nodes */
struct rdma_resource
{
    struct ibv_device_attr device_attr;     /* Device attributes */
    struct ibv_port_attr port_attr;         /* IB port attributes */
    struct ibv_context *context;            /* device handle */
    struct ibv_pd *pd;                      /* PD handle */
    struct ibv_cq *cq;                      /* CQ handle */
    struct ibv_mr *mr;                      /* MR handle */

    struct peer_conn_info peers[MAX_NODES];
};

void *_sock_accept(void *_args);
int sock_listen(struct rdma_resource *rs, struct all_configs *conf);
int sock_connect(struct rdma_resource *rs, struct peer_conn_info *peer, struct fuse_cmd_config *conf);
int sock_sync_data(int sock, int size, void *local_data, void *remote_data);

int create_resources(struct rdma_resource *rs, struct all_configs *conf);
int destroy_resources(struct rdma_resource *rs);

int create_qp(struct rdma_resource *rs, struct peer_conn_info *peer);
int destroy_qp(struct rdma_resource *rs);

int modify_qp_to_init(struct ibv_qp *qp, int ib_port);
int modify_qp_to_rtr(struct ibv_qp *qp, int ib_port, struct peer_conn_info *peer);
int modify_qp_to_rts(struct ibv_qp *qp, struct peer_conn_info *peer);

int connect_qp(struct rdma_resource *rs, struct all_configs *conf, struct peer_conn_info *peer);

int _rdma_post_recv(struct rdma_resource *rs, struct peer_conn_info *peer, uint64_t src, uint64_t length);

#endif // RDMA_H
