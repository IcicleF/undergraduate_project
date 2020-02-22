#include <cluster.h>
#include <debug.h>

int init_self(struct rdma_resource *rs, struct all_configs *conf)
{
    int i;

    if (conf == NULL)
        return -1;
    
    /* Firstly, initialize members of conf */

    if (init_cluster_config(conf->cluster_conf, conf->fuse_cmd_conf->cluster_conf_file) < 0) {
        d_err("failed to initialize cluster config from %s", conf->fuse_cmd_conf->cluster_conf_file);
        return -1;
    }

    if (my_node_conf != NULL)
        d_warn("my_node_conf is already set, reset without freeing");
    my_node_conf = find_my_conf(conf->cluster_conf);

    /* Then, connect with other nodes */

    /* initialize resources */
    if (create_resources(rs, conf) < 0) {
        d_err("failed to create RDMA resources");
        return -1;
    }

    if (my_node_conf->type == NODE_TYPE_CM)
        d_info("I am CM");
    else if (my_node_conf->type == NODE_TYPE_MDS)
        d_info("I am MDS");
    else if (my_node_conf->type == NODE_TYPE_MDS_BAK)
        d_info("I am MDS Replication");
    else if (my_node_conf->type == NODE_TYPE_DS)
        d_info("I am DS");
    else if (my_node_conf->type == NODE_TYPE_CLI)
        d_info("I am Client");
    else {
        d_err("I don't know who I am");
        return -1;
    }

    /* Connect with all other peers BEFORE SELF */
    for (i = 0; i < conf->cluster_conf->node_count; ++i) {
        struct node_config *node_conf = &conf->cluster_conf->node_conf[i];
        if (node_conf->id >= my_node_conf->id)
            continue;
        if (my_node_conf->type == NODE_TYPE_CLI && node_conf->type == NODE_TYPE_CLI)
            continue;
        if (rdma_connect(rs, conf, node_conf->id) < 0) {
            d_err("failed to connect with peer: %d", node_conf->id);
            return -1;
        }
        else
            d_info("successfully connected with peer: %d", node_conf->id);
    }

    /* Wait for incoming connections AFTER SELF */
    if (rdma_listen(rs, conf) < 0) {
        d_err("failed to listen on TCP port %d", conf->fuse_cmd_conf->tcp_port);
        return -1;
    }
    
    return 0;
}

int destroy_self(struct rdma_resource *rs, struct all_configs *conf)
{
    destroy_resources(rs);
    destroy_cluster_config(conf->cluster_conf);

    return 0;
}
