#include <ecal.hpp>
#include <debug.hpp>

/*
 * Constructor initializes `memConf`.
 * Also it initializes `clusterConf`, `myNodeConf` by instantiating an RPCInterface.
 */
ECAL::ECAL()
{
    if (cmdConf == nullptr) {
        d_err("cmdConf should be initialized!");
        exit(-1);
    }
    if (memConf != nullptr)
        d_warn("memConf is already initialized, skip");
    else
        memConf = new MemoryConfig(*cmdConf);
    
    allocTable = new AllocationTable<BlockTy>();
    rpcInterface = new RPCInterface();
    rpcInterface->registerAllocTable(allocTable);

    /* Compute capacity */
    int clusterNodeCount = clusterConf->getClusterSize();
    capacity = (clusterNodeCount * allocTable->getCapacity() / N * K) / (sizeof(Block4K) / sizeof(BlockTy));
}

ECAL::~ECAL()
{
    if (memConf) {
        delete memConf;
        memConf = nullptr;
    }
}

ECAL::GlobalBlock ECAL::readBlock(uint64_t index)
{
    auto pos = getDataPosition(index);
    if (!rpcInterface->isAlive(pos.nodeId))
        return GlobalBlock(degradedReadBlock(index), pos.nodeId, pos.blkNo, true);
    
    if (pos.nodeId == myNodeConf->id)
        return GlobalBlock(allocTable->at(index), pos.nodeId, pos.blkNo, false);
    
    int ret = rpcInterface->remoteReadFrom(pos.nodeId, getDataBlockShift(pos.blkNo),
                                           (uint64_t)(readBuffer.data), sizeof(BlockTy));
    if (ret < 0) {
        d_err("cannot perform RDMA read from peer: %d, block no. %lu", pos.nodeId, pos.blkNo);
        return GlobalBlock();
    }
    return GlobalBlock(&readBuffer, pos.nodeId, pos.blkNo, false);
}
