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

    if (clusterConf->getClusterSize() % N != 0) {
        d_err("FIXME: clusterSize %% N != 0, exit");
        exit(-1);
    }

    /* Compute capacity */
    int clusterNodeCount = clusterConf->getClusterSize();
    capacity = (clusterNodeCount / N * K * allocTable->getCapacity()) / (Block4K::capacity / BlockTy::size);

    /* Initialize EC Matrices */
    gf_gen_cauchy1_matrix(encodeMatrix, N, K);
    ec_init_tables(K, P, &encodeMatrix[K * K], gfTables);

    for (int i = 0; i < P; ++i)
        parity[i] = encodeBuffer + i * BlockTy::size;
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
    int decodeIndex[K], errIndex[K];
    uint8_t *recoverSrc[N], *recoverOutput[N];

    page.index = index;
    memset(page.page.data, 0, Block4K::capacity);

    auto pos = getDataPos(index);
    int errs = 0;
    for (int i = 0, j = 0; i < K && j < N; ++j) {
        int peerId = j + pos.startNodeId;
        if (rpcInterface->isPeerAlive(peerId)) 
            decodeIndex[i++] = j;
        else if (j < K) {
            errIndex[errs] = j;
            recoverOutput[errs++] = page.page.data + j * BlockTy::size;
        }
    }
    
    /* Read data blocks from remote (or self) */
    uint64_t blockShift = getBlockShift(pos.row);
    int taskCnt = 0;
    for (int i = 0; i < K; ++i) {
        int peerId = decodeIndex[i] + pos.startNodeId;
        if (peerId != myNodeConf->id) {
            uint8_t *base = rpcInterface->getRDMASocket()->getReadRegion(peerId);
            rpcInterface->remoteReadFrom(peerId, blockShift, (uint64_t)base, BlockTy::size, i);
            recoverSrc[i] = base;
            ++taskCnt;
        }
        else
            recoverSrc[i] = reinterpret_cast<uint8_t *>(allocTable->at(pos.row));
    }

    ibv_wc wc[2];
    while (taskCnt) {
        taskCnt -= rpcInterface->getRDMASocket()->pollSendCompletion(wc);
        //d_info("wc->status=%d", (int)wc->status);
    }

    /* Copy intact data */
    for (int i = 0; i < K; ++i)
        if (decodeIndex[i] < K)
            memcpy(page.page.data + decodeIndex[i] * BlockTy::size, recoverSrc[i], BlockTy::size);

    if (errs == 0)
        return;
    
    /* Perform decode */
    uint8_t decodeMatrix[N * K];
    uint8_t invertMatrix[N * K];
    uint8_t b[K * K];

    for (int i = 0; i < K; ++i)
        memcpy(&b[K * i], &encodeMatrix[K * decodeIndex[i]], K);
    if (gf_invert_matrix(b, invertMatrix, K) < 0) {
        d_err("cannot do matrix invert!");
        return;
    }
    for (int i = 0; i < errs; ++i)
        memcpy(&decodeMatrix[K * i], &invertMatrix[K * errIndex[i]], K);
    
    uint8_t gfTbls[K * P * 32];
    ec_init_tables(K, errs, decodeMatrix, gfTbls);
    ec_encode_data(BlockTy::size, K, errs, gfTbls, recoverSrc, recoverOutput);
}

void ECAL::writeBlock(ECAL::Page &page)
{
    static uint8_t *data[K];
    
    for (int i = 0; i < K; ++i)
        data[i] = page.page.data + i * BlockTy::size;
    ec_encode_data(BlockTy::size, K, P, gfTables, data, parity);

    DataPosition pos = getDataPos(page.index);
    uint64_t blockShift = getBlockShift(pos.row);
    for (int i = 0; i < N; ++i) {
        int peerId = pos.startNodeId + i;
        uint8_t *blk = (i < K ? data[i] : parity[i - K]);
        
        if (peerId == myNodeConf->id)
            memcpy(allocTable->at(pos.row), blk, BlockTy::size);
        else if (rpcInterface->isPeerAlive(peerId)) {
            uint8_t *base = rpcInterface->getRDMASocket()->getWriteRegion(peerId);
            memcpy(base, blk, BlockTy::size);
            rpcInterface->remoteWriteTo(peerId, blockShift, (uint64_t)base, BlockTy::size);
        }
    }
}
