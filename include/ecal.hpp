/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(ECAL_HPP)
#define ECAL_HPP

#include <isa-l.h>

#include "config.hpp"
#include "datablock.hpp"
#include "network/rdma.hpp"
#include "network/netif.hpp"

class ECAL
{
public:
    struct Page
    {
        Block4K page;
        uint64_t index = 0;

        Page(uint64_t index = 0) : index(index) { }
    };

    static const int K = 2;
    static const int P = 1;
    static const int N = K + P;

    using BlockTy = DataBlock<Block4K::capacity / K>;

public:
    explicit ECAL();
    ~ECAL();
    
    void readBlock(uint64_t index, Page &page);
    void writeBlock(Page &page);

    inline RDMASocket *getRDMASocket() const { return rdma; }

    /* Returns the cluster's capacity in 4kB blocks */
    inline uint64_t getClusterCapacity() const { return capacity; }

    inline void regNetif(NetworkInterface *netif) { this->netif = netif; }

private:
    struct DataPosition
    {
        uint64_t row;
        int startNodeId;

        DataPosition(uint64_t row = 0, int startNodeId = -1) : row(row), startNodeId(startNodeId) { }
        DataPosition(const DataPosition &b) : row(b.row), startNodeId(b.startNodeId) { }
    };

    BlockPool<BlockTy> *allocTable = nullptr;
    RDMASocket *rdma = nullptr;
    uint64_t capacity = 0;

    uint8_t encodeMatrix[N * K];
    uint8_t gfTables[K * P * 32];
    uint8_t encodeBuffer[P * BlockTy::capacity];
    uint8_t *parity[P];                             /* Points to encodeBuffer */

    inline DataPosition getDataPos(uint64_t index)
    {
        int pagePerRow = clusterConf->getClusterSize() / N;
        return DataPosition(index / pagePerRow, 0);
    }
    inline uint64_t getBlockShift(uint64_t index)
    {
        return allocTable->getShift(index);
    }

    NetworkInterface *netif;
};

#endif // ECAL_HPP
