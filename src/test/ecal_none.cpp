#include <ecal.hpp>
#include <debug.hpp>

/**
 * Constructor initializes `memConf`.
 * Also it initializes `clusterConf`, `myNodeConf` by instantiating an RPCInterface.
 * @note This is an alternative implementation of ECAL, which does not implement any
 *       availability strategies. This file should NOT be used in production and is
 *       designed only for performance tests.
 */
ECAL::ECAL()
{
    d_info("ECAL none");
    if (cmdConf == nullptr) {
        d_err("cmdConf should be initialized!");
        exit(-1);
    }
    if (memConf != nullptr)
        d_warn("memConf is already initialized, skip");
    else
        memConf = new MemoryConfig(*cmdConf);
    
    allocTable = new BlockPool<BlockTy>();
    rpcInterface = new RPCInterface();

    if (clusterConf->getClusterSize() % N != 0) {
        d_err("FIXME: clusterSize %% N != 0, exit");
        exit(-1);
    }

    /* Compute capacity */
    int clusterNodeCount = clusterConf->getClusterSize();
    capacity = clusterNodeCount * (allocTable->getCapacity() / (Block4K::capacity / BlockTy::size));
}

ECAL::~ECAL()
{
    if (memConf) {
        delete memConf;
        memConf = nullptr;
    }
}

void ECAL::readBlock(uint64_t index, ECAL::Page &page)
{
    page.index = index;
    uint64_t row = index / N;
    int peerId = index % N;

    if (peerId == myNodeConf->id) {
        memcpy(page.page.data, allocTable->at(row * K), Block4K::size);
        return;
    }

    ibv_wc wc[2];
    uint64_t blockShift = getBlockShift(row * K);
    uint8_t *base = rpcInterface->getRDMASocket()->getReadRegion(peerId);
    rpcInterface->remoteReadFrom(peerId, blockShift, (uint64_t)base, Block4K::size);
    rpcInterface->getRDMASocket()->pollSendCompletion(wc);

    memcpy(page.page.data, base, Block4K::size);
}

void ECAL::writeBlock(ECAL::Page &page)
{
    uint64_t row = page.index / N;
    int peerId = page.index % N;
    if (peerId == myNodeConf->id) {
        memcpy(allocTable->at(row * K), page.page.data, Block4K::size);
        return;
    }

    uint64_t blockShift = getBlockShift(row * K);
    uint8_t *base = rpcInterface->getRDMASocket()->getWriteRegion(peerId);
    memcpy(base, page.page.data, Block4K::size);
    rpcInterface->remoteWriteTo(peerId, blockShift, (uint64_t)base, BlockTy::size, row);
}
