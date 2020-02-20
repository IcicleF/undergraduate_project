#include <cluster.h>
#include <debug.h>

int init_self(struct rdma_resource *rs, struct all_configs *conf)
{
    if (conf == NULL)
        return -1;
    
    /* Firstly, initialize members of conf */

    /* fuse_cmd_conf should have been set by FUSE */
    if (conf->fuse_cmd_conf == NULL) {
        d_err("fuse_cmd_conf should be set by FUSE");
        return -1;
    }

    /* mem_conf can be initialized here? */
    if (conf->mem_conf == NULL) {
        d_warn("init_self allocating mem_conf, this memory won't be freed");
        conf->mem_conf = malloc(sizeof(struct mem_config));
    }
    
    /* cluster_conf can be initialized here */
    if (conf->cluster_conf == NULL) {
        d_warn("init_self allocating cluster_conf, this memory won't be freed");
        conf->cluster_conf = malloc(sizeof(struct cluster_config));
    }
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

    if (my_node_conf->type == NODE_TYPE_CM) {
        d_info("I am Connection Manager");
        d_info("I will wait for incoming connections");
    }
    else if (my_node_conf->type == NODE_TYPE_MDS) {
        d_info("I am MDS");
        d_info("I will ONLY connect with CM");

        if (rdma_connect(rs, conf, conf->cluster_conf->cm_node_id) < 0) {
            d_err("failed to connect with CM");
            return -1;
        }
    }
    else if (my_node_conf->type == NODE_TYPE_MDS_BAK) {
        d_info("I am MDS Replication");
        d_info("I will ONLY connect with MDS & CM");
        
        if (rdma_connect(rs, conf, conf->cluster_conf->cm_node_id) < 0) {
            d_err("failed to connect with CM");
            return -1;
        }
        if (rdma_connect(rs, conf, conf->cluster_conf->mds_node_id) < 0) {
            d_err("failed to connect with MDS");
            return -1;
        }
    }
    else if (my_node_conf->type == NODE_TYPE_DS) {
        int i;

        d_info("I am DS");
        d_info("I will connect to CM, MDS & all other DSs");

        if (rdma_connect(rs, conf, conf->cluster_conf->cm_node_id) < 0) {
            d_err("failed to connect with CM");
            return -1;
        }
        if (rdma_connect(rs, conf, conf->cluster_conf->mds_node_id) < 0) {
            d_err("failed to connect with MDS");
            return -1;
        }
        for (i = 0; i < conf->cluster_conf->node_count; ++i) {
            struct node_config *node_conf = &conf->cluster_conf->node_conf[i];
            if (node_conf->id == my_node_conf->id)
                continue;
            if (node_conf->type == NODE_TYPE_DS)
                if (rdma_connect(rs, conf, node_conf->id) < 0) {
                    d_err("failed to connect with DS: %d", node_conf->id);
                    return -1;
                } 
        }
    }

    /* Wait for incoming connections */
    if (rdma_listen(rs, conf) < 0) {
        d_err("failed to listen on TCP port %d", conf->fuse_cmd_conf->tcp_port);
        return -1;
    }
    
    return 0;
}
