/*
 * config.h
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Define all configuration structures and necessary functions to process them.
 */

#if !defined(CONFIG_H)
#define CONFIG_H

#if !defined(FUSE_USE_VERSION)
#define FUSE_USE_VERSION 39
#endif

#include <arpa/inet.h>
#include <fuse.h>

#include "common.h"

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

    int cm_node_id;
    int mds_node_id;
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
    char *cluster_conf_file;

    char *pmem_dev_name;
    uint64_t pmem_meta_size;            /* Metadata size in bytes */
    uint64_t pmem_pool_size;            /* Data pool size in blocks */
    int tcp_port;

    char *ib_dev_name;
    int ib_port;
};

#define GALOIS_OPTION(t, p) { t, offsetof(struct fuse_cmd_config, p), 1 }

/* All-in-one! */
struct all_configs
{
    struct cluster_config *cluster_conf;
    struct mem_config *mem_conf;

    /* FUSE command line arguments should be initialized separately in main function */
    struct fuse_cmd_config *fuse_cmd_conf;
};

void _set_default_options(struct fuse_cmd_config *conf);
int init_all_configs(struct all_configs *conf, struct fuse_args *args);
int destroy_all_configs(struct all_configs *conf, struct fuse_args *args);

/* Global configuration indicating THIS node.
 * Defined to avoid frequently using `all_configs`.
 */
extern struct node_config *my_node_conf;
extern atomic_bool running;

#endif // CONFIG_H
