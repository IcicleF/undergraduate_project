#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <config.h>
#include <message.h>
#include <debug.h>

int init_cluster_config(struct cluster_config *conf, const char *filename)
{
    FILE *fin;

    int node_id;
    char hostname[MAX_HOSTNAME_LEN];
    char ip_addr[INET_ADDRSTRLEN];
    char node_type[MAX_NODE_TYPE_LEN];
    int i = 0;
    int ret = 0;

    if (conf == NULL || filename == NULL)
        return 0;

    memset(conf, 0, sizeof(struct cluster_config));
    conf->cm_node_id = -1;
    conf->mds_node_id = -1;

    fin = fopen(filename, "r");
    if (fin == NULL) {
        d_err("failed to open cluster config file");
        return -1;
    }
    
    while (fscanf(fin, "%d%s%s%s", &node_id, hostname, ip_addr, node_type) != EOF) {
        /* This might get out of bound, but we will check it immediately */
        struct node_config *node_conf = &conf->node_conf[i];

        if (i >= MAX_NODES) {
            d_err("there must be no more than %d nodes", MAX_NODES);
            ret = -1;
            break;
        }

        if (strcmp("CM", node_type) == 0) {
            node_conf->type = NODE_TYPE_CM;
            if (conf->cm_node_id >= 0)
                d_warn("detected multiple CMs");
            conf->cm_node_id = node_id;
        }
        else if (strcmp("MDS", node_type) == 0) {
            node_conf->type = NODE_TYPE_MDS;
            if (conf->mds_node_id >= 0)
                d_warn("detected multiple major MDSs");
            conf->mds_node_id = node_id;
        }
        else if (strcmp("MDS_BAK", node_type) == 0) {
            node_conf->type = NODE_TYPE_MDS_BAK;
            if (conf->mds_bak_count < MAX_MDS_BAKS)
                conf->mds_bak_node_id[conf->mds_bak_count++] = node_id;
            else
                d_err("detected too many MDS replications (>%d)", MAX_MDS_BAKS);
        }
        else if (strcmp("DS", node_type) == 0) {
            node_conf->type = NODE_TYPE_DS;
            conf->ds_count++;
        }
        else if (strcmp("CLI", node_type) == 0)
            node_conf->type = NODE_TYPE_CLI;
        else {
            d_err("unrecognized node type: %s", node_type);
            ret = -1;
            break;
        }
        node_conf->hostname = strdup(hostname);
        node_conf->id = node_id;
        node_conf->ip_addr = inet_addr(ip_addr);

        ++i;
    }
    
    fclose(fin);

    conf->node_count = i;
    if (ret != 0) {
        destroy_cluster_config(conf);
        return -1;   
    }
    return 0;
}

int destroy_cluster_config(struct cluster_config *conf)
{
    int i;

    if (conf == NULL)
        return 0;
    
    /* Free memory allocated by strdup */
    for (i = 0; i < conf->node_count; ++i)
        free(conf->node_conf[i].hostname);
    return 0;
}

struct node_config *find_conf_by_id(struct cluster_config *conf, int id)
{
    int i = 0;

    if (conf == NULL)
        return NULL;
    
    /* Perform indexing first, then linear search if failed */
    if (id >= 0 && id < conf->node_count && conf->node_conf[id].id == id)
        return &conf->node_conf[id];
    
    for (i = 0; i < conf->node_count; ++i) {
        struct node_config *node_conf = &conf->node_conf[i];
        if (node_conf->id == id)
            return node_conf;
    }
    return NULL;
}

struct node_config *find_conf_by_hostname(struct cluster_config *conf, const char *hostname)
{
    int i = 0;

    if (conf == NULL || hostname == NULL)
        return NULL;

    for (i = 0; i < conf->node_count; ++i) {
        struct node_config *node_conf = &conf->node_conf[i];
        if (strcmp(node_conf->hostname, hostname) == 0)
            return node_conf;
    }
    return NULL;
}

struct node_config *find_conf_by_ip(struct cluster_config *conf, in_addr_t ip_addr)
{
    int i = 0;

    if (conf == NULL)
        return NULL;

    for (i = 0; i < conf->node_count; ++i) {
        struct node_config *node_conf = &conf->node_conf[i];
        if (node_conf->ip_addr == ip_addr)
            return node_conf;
    }
    return NULL;
}

struct node_config *find_conf_by_ip_str(struct cluster_config *conf, const char *ip_addr)
{
    in_addr_t ip = inet_addr(ip_addr);
    return find_conf_by_ip(conf, ip);
}

struct node_config *find_my_conf(struct cluster_config *conf)
{
    static char hostname[MAX_HOSTNAME_LEN];
    
    gethostname(hostname, MAX_HOSTNAME_LEN);
    return find_conf_by_hostname(conf, hostname);
}


/*
 * Initializes `mem_conf` from `fuse_cmd_conf`.
 * Creates memory mapping for the pmem device specified.
 */
int init_mem_config(struct mem_config *mem_conf, struct fuse_cmd_config *fuse_cmd_conf)
{
    const char *pmemdev = fuse_cmd_conf->ib_dev_name;
    uint64_t size = fuse_cmd_conf->pmem_size;
    int fd;

    if (mem_conf->mem_loc != NULL)
        d_warn("mem_loc != NULL, might be initialized, reset");

    fd = open(pmemdev, O_RDWR);
    if (fd < 0) {
        d_err("cannot open pmem device: %s", pmemdev);
        return -1;
    }

    mem_conf->mem_loc = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem_conf->mem_loc == NULL) {
        d_err("cannot perform mmap (size: %lu)", size);
        close(fd);
        return -1;
    }

    mem_conf->mem_size = size;

    /* Initialize other members of `mem_conf` */
    mem_conf->rpc_buf_loc = mem_conf->mem_loc;
    mem_conf->rpc_buf_size = sizeof(struct message) * MAX_NODES;

    mem_conf->alloc_table_loc = mem_conf->rpc_buf_loc + mem_conf->rpc_buf_size;
    mem_conf->alloc_table_size = mem_conf->mem_size - mem_conf->rpc_buf_size;

    return 0;
}

void mem_full_flush(struct mem_config *conf)
{
    msync(conf->mem_loc, conf->mem_size, MS_SYNC);
}


static const struct fuse_opt option_spec[] = {
    GALOIS_OPTION("--cluster_conf_file=%s", cluster_conf_file),
	GALOIS_OPTION("--pmem_dev=%s", pmem_dev_name),
    GALOIS_OPTION("--pmem_size=%lu", pmem_size),
    GALOIS_OPTION("--tcp_port=%d", tcp_port),
    GALOIS_OPTION("--ib_dev=%s", ib_dev_name),
    GALOIS_OPTION("--ib_port=%d", ib_port),
	FUSE_OPT_END
};

void _set_default_options(struct fuse_cmd_config *conf)
{
    conf->cluster_conf_file = strdup("cluster.conf");
    conf->pmem_dev_name = strdup("/dev/pmem0");
    conf->pmem_size = 1 << (31 - 12);              // 2GB
    conf->ib_dev_name = strdup("ib0");
    conf->ib_port = 1;
}

int alloc_all_configs(struct all_configs *conf, struct fuse_args *args)
{
    memset(conf, 0, sizeof(struct all_configs));

    /* Parse FUSE arguments */
    conf->fuse_cmd_conf = malloc(sizeof(struct fuse_cmd_config));
    memset(conf->fuse_cmd_conf, 0, sizeof(struct fuse_cmd_config));

    _set_default_options(conf->fuse_cmd_conf);

    if (fuse_opt_parse(args, conf->fuse_cmd_conf, option_spec, NULL) == -1)
		return -1;

    /* Memory configuration */
    conf->mem_conf = malloc(sizeof(struct mem_config));
    memset(conf->mem_conf, 0, sizeof(struct mem_config));
    
    /* Cluster configuration */
    conf->cluster_conf = malloc(sizeof(struct cluster_config));
    memset(conf->cluster_conf, 0, sizeof(struct cluster_config));
    
    return 0;
}

int dealloc_all_configs(struct all_configs *conf, struct fuse_args *args)
{
    if (conf->cluster_conf != NULL) {
        d_info("dealloc_all_configs deallocating cluster_conf");
        free(conf->cluster_conf);
        conf->cluster_conf = NULL;
    }
    else
        d_warn("cluster_conf is already freed");
    
    if (conf->mem_conf != NULL) {
        d_info("dealloc_all_configs deallocating mem_conf");
        free(conf->mem_conf);
        conf->mem_conf = NULL;
    }
    else
        d_warn("mem_conf is already freed");
    
    fuse_opt_free_args(args);

    return 0;
}
