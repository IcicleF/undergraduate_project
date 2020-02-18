#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>

#include <config.h>
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

        if (strcmp("CM", node_type) == 0)
            node_conf->type = NODE_TYPE_CM;
        else if (strcmp("MDS", node_type) == 0)
            node_conf->type = NODE_TYPE_MDS;
        else if (strcmp("MDS_BAK", node_type) == 0)
            node_conf->type = NODE_TYPE_MDS_BAK;
        else if (strcmp("DS", node_type) == 0)
            node_conf->type = NODE_TYPE_DS;
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
