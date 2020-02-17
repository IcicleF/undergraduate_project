#if !defined(COMMONS_H)
#define COMMONS_H

#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <malloc.h>
#include <errno.h>

#define MAX_NODES               32
#define MAX_PEERS               MAX_NODES

#define MAX_HOSTNAME_LEN        128

#define MAX_QP_DEPTH            64
#define MAX_DEST_RD_ATOMIC      16
#define PSN_MAGIC               4396

#define NODE_TYPE_CM            0x10            // Cluster Manager
#define NODE_TYPE_MDS           0x20            // Metadata Server (Centralized)
#define NODE_TYPE_MDS_BAK       0x21            // Metadata Server (Slave Replication)
#define NODE_TYPE_DS            0x30            // Data Server
#define NODE_TYPE_CLI           0x40            // Client
#define MAX_NODE_TYPE_LEN       8

#endif // COMMONS_H
