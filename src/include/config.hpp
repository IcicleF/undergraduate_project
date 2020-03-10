/*
 * config.hpp
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Define all configuration structures and necessary functions to process them.
 */

#if !defined(CONFIG_HPP)
#define CONFIG_HPP

#if !defined(FUSE_USE_VERSION)
#define FUSE_USE_VERSION 39
#endif

#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "commons.hpp"

/* FUSE command line arguments */
struct CmdLineConfig
{
    CmdLineConfig();

    std::string clusterConfigFile;      /* Cluster configuration file name */
    std::string pmemDeviceName;         /* Persistent memory device (e.g. /dev/dax0.0) */
    uint64_t pmemSize;                  /* Data pool size in blocks */
    std::string ipv6PortStr;            /* IPv6 port (string) to establish connections */
    std::string ibDeviceName;           /* InfiniBand device name (e.g. ib0) */
    int ibPort;                         /* InfiniBand port */
};

#define GALOIS_OPTION(t, p) { t, offsetof(CmdLineConfig, p), 1 }

/* Node type */
enum NodeType
{
    NODE_UNDEF = 0,
    NODE_CM = 1,
    NODE_DS = 2,
    NODE_CLI = 9
};

/* Node configuration */
struct NodeConfig
{
    int id = -1;
    std::string hostname;
    std::string ipv6AddrStr;
    addrinfo ai;
    NodeType type;
};

/* Cluster configuration (in whole) */
class ClusterConfig
{
public:
    explicit ClusterConfig(std::string filename);
    ~ClusterConfig() = default;

    __always_inline int getClusterSize() const { return nodeCount; }
    __always_inline int getCMId() const { return cmId; }
    __always_inline NodeConfig operator[](int index) const { return nodeConf[index]; }
    std::optional<NodeConfig> findConfById(int id) const;
    std::optional<NodeConfig> findConfByHostname(const std::string &hostname) const;
    std::optional<NodeConfig> findConfByIPv6Str(const std::string &ipv6AddrStr) const;
    std::optional<NodeConfig> findMyself() const;
    __always_inline std::set<int> getNodeIdSet() const { return nodeIds; }

private:
    NodeConfig nodeConf[MAX_NODES];
    std::set<int> nodeIds;
    std::unordered_map<std::string, int> ip2id;         /* IPv6 address string to nodeId */
    std::unordered_map<std::string, int> host2id;       /* Hostname to nodeId */
    int nodeCount;
    int cmId;
};

/* Memory organization configuration */
class MemoryConfig
{
public:
    explicit MemoryConfig(uint64_t base, uint64_t capacity);
    explicit MemoryConfig(const CmdLineConfig &conf);
    ~MemoryConfig() = default;

    __always_inline void fullSync() const { msync((void *)base, capacity, MS_SYNC); }
    
    __always_inline void *getMemory() const { return (void *)base; }
    __always_inline void *getSendBuffer(int peerId) const { return (void *)(sendBufferBase + RDMA_BUF_SIZE * peerId); }
    __always_inline void *getReceiveBuffer(int peerId) const { return (void *)(recvBufferBase + RDMA_BUF_SIZE * peerId); }
    __always_inline void *getDataArea() const { return (void *)dataArea; }

    __always_inline uint64_t getSendBufferShift(int peerId) const { return (uint64_t)getSendBuffer(peerId) - base; }
    __always_inline uint64_t getReceiveBufferShift(int peerId) const { return (uint64_t)getReceiveBuffer(peerId) - base; }
    __always_inline uint64_t getDataAreaShift() const { return (uint64_t)getDataArea() - base; }

    __always_inline uint64_t getCapacity() const { return capacity; }
    __always_inline uint64_t getDataAreaCapacity() const { return dataAreaCapacity; }

private:
    void calcBaseAddresses();

private:
    uint64_t base = 0;
    uint64_t sendBufferBase = 0;
    uint64_t recvBufferBase = 0;
    uint64_t dataArea = 0;

    uint64_t capacity = 0;
    uint64_t dataAreaCapacity = 0;

    int fd = -1;
};

extern CmdLineConfig *cmdConf;
extern ClusterConfig *clusterConf;
extern MemoryConfig *memConf;
extern NodeConfig *myNodeConf;

/* Shutdown this flag to stop detached threads from running */
extern std::atomic<bool> isRunning;

#endif // CONFIG_HPP
