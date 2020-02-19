#if !defined(CONFIG_H)
#define CONFIG_H

#include <arpa/inet.h>
#include "commons.h"

/* Node configuration */
struct node_config
{
    char *hostname;
    int id;
    in_addr_t ip_addr;

    int type;
};

/* Cluster configuration (in whole) */
struct cluster_config
{
    struct node_config node_conf[MAX_NODES];
    int node_count;
};

int init_cluster_config(struct cluster_config *conf, const char *filename);
int destroy_cluster_config(struct cluster_config *conf);

struct node_config *find_conf_by_id(struct cluster_config *conf, int id);
struct node_config *find_conf_by_hostname(struct cluster_config *conf, const char *hostname);
struct node_config *find_conf_by_ip(struct cluster_config *conf, in_addr_t ip_addr);
struct node_config *find_conf_by_ip_str(struct cluster_config *conf, const char *ip_addr);
struct node_config *find_my_conf(struct cluster_config *conf);

/* Memory organization configuration */
struct mem_config
{
    void *mem_loc;
    uint64_t mem_size;
};

/* FUSE command line arguments */
struct fuse_cmd_config
{
    char *pmem_dev_name;
    int tcp_port;

    char *ib_dev_name;
    int ib_port;
};

#define GALOIS_OPTION(t, p) { t, offsetof(struct fuse_cmd_config, p), 1 }

/* All-in-one! */
struct all_configs
{
    struct cluster_config *cluster_conf;
    struct node_config *my_node_conf;           /* Should point to one stored in cluster_conf */
    struct mem_config *mem_conf;
    struct fuse_cmd_config *fuse_cmd_conf;

    struct                                      /* Store running-state related information */ 
    {
        atomic_bool running;
    };
};

#endif // CONFIG_H
