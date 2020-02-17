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
    uint8_t gid[16];                        /* GID */
} __attribute__((packed));

/* Store the RDMA connection data with a peer */
struct peer_conn_info
{
    struct ibv_qp *qp;                      /* QP handle */
    struct ibv_cq *cq;                      /* CQ handle */

    struct cm_conn_info conn_data;
    int sock;                               /* TCP socket */
    int node_id;                            /* Peer's node ID */
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

int create_resources(struct rdma_resource *rs, struct all_configs *conf);
int destroy_resources(struct rdma_resource *rs);

int create_qp(struct rdma_resource *rs, struct peer_conn_info *peer);
// int destroy_qp()   TODO

int modify_qp_to_init(struct ibv_qp *qp, int ib_port);
int modify_qp_to_rtr(struct ibv_qp *qp, int ib_port, struct peer_conn_info *peer);
int modify_qp_to_rts(struct ibv_qp *qp, struct peer_conn_info *peer);

#endif // RDMA_H
