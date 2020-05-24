/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(CONFIG_HPP)
#define CONFIG_HPP

#if !defined(FUSE_USE_VERSION)
#define FUSE_USE_VERSION 39
#endif

#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "commons.hpp"

/* FUSE command line arguments */
struct CmdLineConfig
{
    CmdLineConfig();

    std::string clusterConfigFile;      /* Cluster configuration file name */
    std::string pmemDeviceName;         /* Persistent memory device (e.g. /dev/dax0.0) */
    int tcpPort;                        /* RDMA listen port */
    int udpPort;                        /* ERPC management port */
    uint64_t pmemSize;                  /* Data pool size in blocks */
    bool recover;                       /* Indicate whether this is a recovery */

    int _N;
    int _Size;
    int _Thread;
};

#define GALOIS_OPTION(t, p) { t, offsetof(CmdLineConfig, p), 1 }

/* Node type */
enum NodeType
{
    NODE_UNDEF = 0,
    NODE_SERVER = 0x10,
    NODE_DMS,
    NODE_FMS,
    NODE_CLIENT = 0x20
};

/* Node configuration */
struct NodeConfig
{
    int id = -1;                        /* Pre-configured node ID */
    std::string hostname;               /* Node's host name */
    std::string ipAddrStr;              /* Node's IPv4 address */
    std::string ibDevIPAddrStr;         /* Node's IB device IPv4 address */
    NodeType type;                      /* Node's type */
};

/* Cluster configuration (in whole) */
class ClusterConfig
{
public:
    explicit ClusterConfig(std::string filename);
    ~ClusterConfig();

    __always_inline int getClusterSize() const { return nodeCount; }
    __always_inline int getCMId() const { return cmId; }
    __always_inline NodeConfig operator[](int index) const { return nodeConf[index]; }
    NodeConfig findConfById(int id) const;
    NodeConfig findConfByHostname(const std::string &hostname) const;
    NodeConfig findConfByIPStr(const std::string &ipAddrStr) const;
    NodeConfig findMyself() const;
    __always_inline std::set<int> getNodeIdSet() const { return nodeIds; }

private:
    NodeConfig nodeConf[MAX_NODES];
    std::set<int> nodeIds;
    std::unordered_map<std::string, int> ip2id;         /* IP address string to nodeId */
    std::unordered_map<std::string, int> host2id;       /* Hostname to nodeId */
    int nodeCount;
    int cmId;
};

/* Memory organization configuration */
class MemoryConfig
{
public:
    explicit MemoryConfig(uint64_t base, uint64_t capacity) : base(base), capacity(capacity) { }
    explicit MemoryConfig(const CmdLineConfig &conf);
    ~MemoryConfig();

    __always_inline void fullSync() const { msync((void *)base, capacity, MS_SYNC); }

    __always_inline void *getMemory() const { return (void *)base; }
    __always_inline uint64_t getCapacity() const { return capacity; }

private:
    void calcBaseAddresses();

private:
    uint64_t base = 0;
    uint64_t capacity = 0;

    int fd = -1;
};

extern CmdLineConfig *cmdConf;
extern ClusterConfig *clusterConf;
extern MemoryConfig *memConf;
extern NodeConfig *myNodeConf;

/* Shutdown this flag to stop detached threads from running */
extern std::atomic<bool> isRunning;

#endif // CONFIG_HPP
