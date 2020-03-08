#include <netdb.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fstream>

#include <config.hpp>
#include <message.hpp>
#include <debug.hpp>

using namespace std;

CmdLineConfig *cmdConf = nullptr;
ClusterConfig *clusterConf = nullptr;
MemoryConfig *memConf = nullptr;
NodeConfig *myNodeConf = nullptr;
std::atomic<bool> isRunning;

// CmdLineConfig part

/*
 * Set default parameters.
 * New memory is allocated for FUSE to free it later.
 */
void CmdLineConfig::setAsDefault()
{
    if (clusterConfigFile || pmemDeviceName || ibDeviceName) {
        d_err("looks like default values already set, skip");
        return;
    }
    clusterConfigFile = strdup("cluster.conf");
    pmemDeviceName = strdup("/dev/pmem0");
    pmemSize = 1 << (31 - 12);                  // 2GB
    ibDeviceName = strdup("ib0");
    ibPort = 1;
}

/* Parse arguments from a fuse_args instance */
void CmdLineConfig::initFromFuseArgs(fuse_args *args)
{
    static const struct fuse_opt optionSpec[] = {
        GALOIS_OPTION("--cluster_conf_file=%s", clusterConfigFile),
        GALOIS_OPTION("--pmem_dev=%s", pmemDeviceName),
        GALOIS_OPTION("--pmem_size=%lu", pmemSize),
        GALOIS_OPTION("--tcp_port=%d", tcpPort),
        GALOIS_OPTION("--ib_dev=%s", ibDeviceName),
        GALOIS_OPTION("--ib_port=%d", ibPort),
        FUSE_OPT_END
    };

    /* First initialize default values */
    setAsDefault();

    if (fuse_opt_parse(args, this, optionSpec, NULL) == -1)
        d_err("failed to parse FUSE args");
}

// ClusterConfig part

ClusterConfig::ClusterConfig(string filename)
{
    int nodeId;
    string hostname;
    string ipAddrStr;
    string nodeType;

    ifstream fin(filename);
    if (!fin) {
        d_err("cannot open cluster config file: %s", filename.c_str());
        exit(-1);
    }
    
    int i;
    for (i = 0; fin >> nodeId >> hostname >> ipAddrStr >> nodeType; ++i) {
        if (i >= MAX_NODES) {
            d_err("there must be no more than %d nodes", MAX_NODES);
            exit(-1);
        }

        if (nodeIds.find(nodeId) != nodeIds.end()) {
            d_err("duplicate node ID: %d", nodeId);
            exit(-1);
        }
        nodeIds.insert(nodeId);
        nodeConf[nodeId].id = nodeId;
        nodeConf[nodeId].hostname = hostname;
        nodeConf[nodeId].ipAddr = inet_addr(ipAddrStr.c_str());
        ip2id[nodeConf[nodeId].ipAddr] = nodeId;
        host2id[hostname] = nodeId;

        if (nodeType == "CM") {
            nodeConf[nodeId].type = NODE_CM;
            cmId = nodeId;
        }
        else if (nodeType == "DS")
            nodeConf[nodeId].type = NODE_DS;
        else if (nodeType == "CLI")
            nodeConf[nodeId].type = NODE_CLI;
        else {
            d_err("unrecognized node type: %s", nodeType.c_str());
            return;
        }
    }
    nodeCount = i;
    fin.close();
}

optional<NodeConfig> ClusterConfig::findConfById(int id) const
{
    if (id >= 0 && id < nodeCount && nodeConf[id].id == id)
        return nodeConf[id];
    return {};
}

optional<NodeConfig> ClusterConfig::findConfByHostname(const string &hostname) const
{
    auto it = host2id.find(hostname);
    if (it != host2id.end())
        return nodeConf[it->second];
    return {};
}

optional<NodeConfig> ClusterConfig::findConfByIp(in_addr_t ipAddr) const
{
    auto it = ip2id.find(ipAddr);
    if (it != ip2id.end())
        return nodeConf[it->second];
    return {};
}

optional<NodeConfig> ClusterConfig::findConfByIpStr(const string &ipAddrStr) const
{
    return findConfByIp(inet_addr(ipAddrStr.c_str()));
}

optional<NodeConfig> ClusterConfig::findMyself() const
{
    static char hostname[MAX_HOSTNAME_LEN];
    gethostname(hostname, MAX_HOSTNAME_LEN);
    return findConfByHostname(hostname);
}

// MemoryConfig part

MemoryConfig::MemoryConfig(uint64_t base, uint64_t capacity)
        : base(base), capacity(capacity)
{
    calcBaseAddresses();
}

/*
 * Initializes from FUSE command-line-specified arguments.
 * Creates memory mapping for the pmem device specified.
 */
MemoryConfig::MemoryConfig(const CmdLineConfig &conf)
{
    if (base != 0)
        d_warn("might have been initialized, reset");

    fd = open(conf.pmemDeviceName, O_RDWR);
    if (fd < 0) {
        d_err("cannot open pmem device: %s", conf.pmemDeviceName);
        exit(-1);
    }

    base = (uint64_t)mmap(NULL, conf.pmemSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (unlikely(base == 0)) {
        d_err("cannot perform mmap (size: %lu, err: %s)", conf.pmemSize, strerror(errno));
        close(fd);
        exit(-1);
    }

    capacity = conf.pmemSize;
    calcBaseAddresses();
}

void MemoryConfig::calcBaseAddresses()
{
    uint64_t srBufferSize = RDMA_BUF_SIZE * MAX_NODES;
    
    sendBufferBase = base;
    recvBufferBase = sendBufferBase + srBufferSize;
    dataArea = recvBufferBase + srBufferSize;
    dataAreaCapacity = capacity - 2 * srBufferSize;
    
    if (dataAreaCapacity <= 0) {
        dataAreaCapacity = 0;
        d_err("NO SPACE FOR DATA!");
    }
}
