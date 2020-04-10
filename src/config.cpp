#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fstream>

#include <config.hpp>
#include <debug.hpp>
#include <network/message.hpp>

using namespace std;

CmdLineConfig *cmdConf = nullptr;
ClusterConfig *clusterConf = nullptr;
MemoryConfig *memConf = nullptr;
NodeConfig *myNodeConf = nullptr;
std::atomic<bool> isRunning;

// CmdLineConfig part

/**
 * Set default parameters.
 * New memory is allocated for FUSE to free it later.
 */
CmdLineConfig::CmdLineConfig()
{
    char *env;

    if ((env = getenv("CLUSTERCONF")))
        clusterConfigFile = env;
    else
        clusterConfigFile = "cluster.conf";
    
    if ((env = getenv("PMEMDEV")))
        pmemDeviceName = env;
    else
        pmemDeviceName = "/mnt/gjfs/sim0";
    
    if ((env = getenv("PMEMSZ")))
        pmemSize = std::stoi(std::string(env));
    else
        pmemSize = 1lu << 20;
    
    if ((env = getenv("PORT")))
        tcpPort = std::stoi(std::string(env));
    else
        tcpPort = 40345;

    recover = ((env = getenv("RECOVER")) && strcmp(env, "OFF") && strcmp(env, "NO"));

    /* getenv results should NOT be freed, so it is left as is */
    d_info("pmem: %s", pmemDeviceName.c_str());
    d_info("pmem size: %lu", pmemSize);
    d_info("tcp port: %d", tcpPort);
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
    string nodeTypeStr;

    ifstream fin(filename);
    if (!fin) {
        d_err("cannot open cluster config file: %s", filename.c_str());
        exit(-1);
    }
    
    int i;
    for (i = 0; fin >> nodeId >> hostname >> ipAddrStr >> ibDevIPAddrStr >> nodeTypeStr; ++i) {
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
        if (nodeTypeStr == "DMS")
            nodeConf[nodeId].type = NODE_DMS;
        else if (nodeTypeStr == "FMS")
            nodeConf[nodeId].type = NODE_FMS;
        else if (nodeTypeStr == "DS")
            nodeConf[nodeId].type = NODE_DS;
        else if (nodeTypeStr == "CLI")
            nodeConf[nodeId].type = NODE_CLI;
        else
            d_err("unrecognized node type: %s", nodeTypeStr.c_str());
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

/**
 * Initializes from command line arguments.
 * Creates memory mapping for the pmem device specified.
 */
MemoryConfig::MemoryConfig(const CmdLineConfig &conf)
{
    bool expected = false;
    if (!pmemOccupied.compare_exchange_weak(expected, true)) {
        expectTrue(expected);
        d_err("pmem device has been occupied!");
        exit(-1);
    }
    
    if (base != 0)
        d_warn("might have been initialized, reset");

    fd = open(conf.pmemDeviceName.c_str(), O_RDWR);
    if (fd < 0) {
        d_err("cannot open pmem device: %s", conf.pmemDeviceName.c_str());
        exit(-1);
    }

    base = (uint64_t)mmap(NULL, conf.pmemSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (Unlikely(base == 0)) {
        d_err("cannot perform mmap (size: %lu, err: %s)", conf.pmemSize, strerror(errno));
        close(fd);
        exit(-1);
    }

    capacity = conf.pmemSize;
    d_info("MemoryConfig: [%p, %p)", (void *)base, (void *)(base + capacity));
}

MemoryConfig::~MemoryConfig()
{
    if (base != 0)
        munmap((void *)base, capacity);
}
