/*
 * ecal.h
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * Implements a midware to provide highly-available access to distributed NVM.
 * The LRC (Local Reconstruction Code) erasure coding is used to provide availability.
 * Provides block-granularity access to higher levels.
 * 
 * Based on ISA-L.
 */

#if !defined(ECAL_HPP)
#define ECAL_HPP

#include <isa-l.h>

#include "config.hpp"
#include "alloctable.hpp"
#include "rpc.hpp"

class ECAL
{
public:
    struct Page
    {
        Block4K page;
        uint64_t index = 0;

        Page(uint64_t index = 0) : index(index) { }
    };

    using BlockTy = RPCInterface::BlockTy;

    static const int K = 2;
    static const int P = 1;
    static const int N = K + P;

public:
    explicit ECAL();
    ~ECAL();
    
    void readBlock(uint64_t index, Page &page);
    void writeBlock(Page &page);

    __always_inline RPCInterface *getRPCInterface() const { return rpcInterface; }

    /* Returns the cluster's capacity in 4kB blocks */
    __always_inline uint64_t getClusterCapacity() const { return capacity; }

private:
    struct DataPosition
    {
        uint64_t row;
        int startNodeId;

        DataPosition(uint64_t row = 0, int startNodeId = -1) : row(row), startNodeId(startNodeId) { }
        DataPosition(const DataPosition &b) : row(b.row), startNodeId(b.startNodeId) { }
    };

    AllocationTable<BlockTy> *allocTable = nullptr;
    RPCInterface *rpcInterface = nullptr;
    uint64_t capacity = 0;

    uint8_t encodeMatrix[N * K];
    uint8_t gfTables[K * P * 32];
    uint8_t encodeBuffer[P * BlockTy::capacity];
    uint8_t *parity[P];

    __always_inline DataPosition getDataPos(uint64_t index)
    {
        int pagePerRow = clusterConf->getClusterSize() / N;
        return DataPosition(index / pagePerRow, (index % pagePerRow) * N);
    }
    __always_inline uint64_t getBlockShift(uint64_t index)
    {
        return memConf->getDataAreaShift() + allocTable->getShift(index);
    }
};

#endif // ECAL_HPP
