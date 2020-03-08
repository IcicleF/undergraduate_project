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
    using BlockTy = RPCInterface::BlockTy;

    static const int K = 4;
    static const int P = 2;
    static const int N = K + P;

    BlockTy readBuffer;
    struct GlobalBlock
    {
        BlockTy *block;         /* Local buffer */
        int nodeId;             /* The original position, node ID */
        uint64_t blockNo;       /* The original position, # of block */
        bool degraded;          /* If true, then this read is degraded */

        GlobalBlock(BlockTy *block = nullptr, int nodeId = -1, uint64_t blockNo = 0, bool degraded = false)
                : block(block), nodeId(nodeId), blockNo(blockNo), degraded(degraded) { }
    };

public:
    explicit ECAL();
    ~ECAL();

    GlobalBlock readBlock(uint64_t index);
    int writeBlock(GlobalBlock block);
    
    /* Returns the cluster's capacity in 4KB BLOCKS */
    __always_inline uint64_t getClusterCapacity() const { return capacity; }

private:
    struct DataPosition
    {
        int nodeId;
        uint64_t blkNo;
    };

    AllocationTable<BlockTy> *allocTable = nullptr;
    RPCInterface *rpcInterface = nullptr;
    uint64_t capacity = 0;

    __always_inline uint64_t getDataBlockShift(uint64_t index) const
    {
        return memConf->getDataAreaShift() + allocTable->getShift(index);
    }

    DataPosition getDataPosition(uint64_t index);
    BlockTy *degradedReadBlock(uint64_t index);
    int recoverWriteBlock(uint64_t index);
};

#endif // ECAL_HPP
