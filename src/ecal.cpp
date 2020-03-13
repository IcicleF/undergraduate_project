#include <chrono>

#include <ecal.hpp>
#include <debug.hpp>

using namespace std;
using namespace std::chrono;

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

extern long wrdmaus, rrdmaus;
extern long wisalus, risalus;

ECAL::Page ECAL::readBlock(uint64_t index)
{
    static BlockTy readBuffer[K];
    static BlockTy decodeBuffer[N];
    static int decodeIndex[K], errIndex[K];

    Page res(index);
    memset(&res.page, 0, Block4K::capacity);

    auto pos = getDataPos(index);
    int errs = 0;
    for (int i = 0, j = pos.startNodeId; i < K && j < pos.startNodeId + N; ++j)
        if (rpcInterface->isAlive(j))
            decodeIndex[i++] = j;
        else if (j < pos.startNodeId + K)
            errIndex[errs++] = j;
    
    /* Read data blocks from remote (or self) */
    uint64_t blockShift = getBlockShift(pos.row);
    for (int i = 0; i < K; ++i) {
        if (decodeIndex[i] != myNodeConf->id) {
            auto start = steady_clock::now();
            int ret = rpcInterface->remoteReadFrom(decodeIndex[i], blockShift,
                    (uint64_t)(readBuffer + i), BlockTy::size);
            auto end = steady_clock::now();
            rrdmaus += duration_cast<microseconds>(end - start).count();
            if (ret < 0) {
                d_err("failed to RDMA read from peer: %d, block set to zero", decodeIndex[i]);
                memset(readBuffer + i, 0, BlockTy::size);
            }
        }
        else
            memcpy(readBuffer + i, allocTable->at(pos.row), BlockTy::size);
    }

    if (errs == 0)
        memcpy(&res.page, readBuffer, Block4K::capacity);
    else {
        /* Perform decode */
        uint8_t decodeMatrix[N * K];
        uint8_t invertMatrix[N * K];
        uint8_t b[K * K];

        auto start = steady_clock::now();

        for (int i = 0; i < K; ++i)
            decodeIndex[i] -= pos.startNodeId;
        for (int i = 0; i < K; ++i)
            for (int j = 0; j < K; ++j)
                b[K * i + j] = encodeMatrix[K * decodeIndex[i] + j];
        if (gf_invert_matrix(b, invertMatrix, K) < 0) {
            d_err("cannot do matrix invert, return all zero");
            return res;
        }
        for (int i = 0; i < errs; ++i)
            for (int j = 0; j < K; ++j)
                decodeMatrix[K * i + j] = invertMatrix[K * errIndex[i] + j];
        
        uint8_t *recoverSrc[N];
        uint8_t *recoverOutput[N];
        for (int i = 0; i < K; ++i) {
            recoverSrc[i] = readBuffer[i].data;
            recoverOutput[i] = decodeBuffer[i].data;
        }
        
        uint8_t gfTbls[K * P * 32];
        ec_init_tables(K, errs, decodeMatrix, gfTbls);
        ec_encode_data(BlockTy::size, K, errs, gfTbls, recoverSrc, recoverOutput);

        auto end = steady_clock::now();
        risalus += duration_cast<microseconds>(end - start).count();

        for (int i = 0; i < K; ++i)
            if (decodeIndex[i] < K)
                memcpy(res.page.data + BlockTy::size * decodeIndex[i], readBuffer[i].data, BlockTy::size);
        for (int i = 0; i < errs; ++i)
            memcpy(res.page.data + BlockTy::size * errIndex[i], recoverOutput[i], BlockTy::size);
    }

    return res;
}

void ECAL::writeBlock(ECAL::Page page)
{
    static uint8_t *data[K];
    
    for (int i = 0; i < K; ++i)
        data[i] = page.page.data + i * BlockTy::size;
    
    auto start = steady_clock::now();
    ec_encode_data(BlockTy::size, K, P, gfTables, data, parity);
    auto end = steady_clock::now();
    wisalus += duration_cast<microseconds>(end - start).count();

    DataPosition pos = getDataPos(page.index);
    uint64_t blockShift = getBlockShift(pos.row);
    for (int i = 0; i < N; ++i) {
        int nodeId = pos.startNodeId + i;
        uint8_t *blk = (i < K ? data[i] : parity[i - K]);
        
        if (nodeId == myNodeConf->id)
            memcpy(allocTable->at(pos.row), blk, BlockTy::capacity);
        else {
            start = steady_clock::now();
            int ret = rpcInterface->remoteWriteTo(nodeId, blockShift,
                    (uint64_t)blk, BlockTy::capacity);
            end = steady_clock::now();
            wrdmaus += duration_cast<microseconds>(end - start).count();
            if (ret < 0) {
                d_err("failed to RDMA write to peer: %d", nodeId);
            }
        }
    }
}
