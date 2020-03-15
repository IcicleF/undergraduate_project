#include <netdb.h>
#include <arpa/inet.h>
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
CmdLineConfig::CmdLineConfig()
{
    clusterConfigFile = "cluster.conf";
    pmemDeviceName = "/mnt/gjfs/sim0";
    pmemSize = 1lu << 32;
    tcpPort = 34343;
}

// ClusterConfig part

ClusterConfig::ClusterConfig(string filename)
{
    if (!cmdConf) {
        d_err("cmdConf should have been initialized!");
        exit(-1);
    }

    int nodeId;
    string hostname;
    string ipAddrStr;
    string ibDevIPAddrStr;

    ifstream fin(filename);
    if (!fin) {
        d_err("cannot open cluster config file: %s", filename.c_str());
        exit(-1);
    }
    
    int i;
    for (i = 0; fin >> nodeId >> hostname >> ipAddrStr >> ibDevIPAddrStr; ++i) {
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
        nodeConf[nodeId].ipAddrStr = ipAddrStr;
	nodeConf[nodeId].ibDevIPAddrStr = ibDevIPAddrStr;
        nodeConf[nodeId].type = NODE_DS;
        ip2id[nodeConf[nodeId].ipAddrStr] = nodeId;
        host2id[hostname] = nodeId;
    }
    nodeCount = i;
    fin.close();
}

NodeConfig ClusterConfig::findConfById(int id) const
{
    if (id >= 0 && id < nodeCount && nodeConf[id].id == id)
        return nodeConf[id];
    return NodeConfig();
}

NodeConfig ClusterConfig::findConfByHostname(const string &hostname) const
{
    auto it = host2id.find(hostname);
    if (it != host2id.end())
        return nodeConf[it->second];
    return NodeConfig();
}

NodeConfig ClusterConfig::findConfByIPStr(const string &ipAddrStr) const
{
    auto it = ip2id.find(ipAddrStr);
    if (it != ip2id.end())
        return nodeConf[it->second];
    return NodeConfig();
}

NodeConfig ClusterConfig::findMyself() const
{
    static char hostname[MAX_HOSTNAME_LEN];
    gethostname(hostname, MAX_HOSTNAME_LEN);
    hostent *hent = gethostbyname(hostname);
    string ipAddr = inet_ntoa(*(in_addr *)(hent->h_addr_list[0]));
    return findConfByIPStr(ipAddr);
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

    fd = open(conf.pmemDeviceName.c_str(), O_RDWR);
    if (fd < 0) {
        d_err("cannot open pmem device: %s", conf.pmemDeviceName.c_str());
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
