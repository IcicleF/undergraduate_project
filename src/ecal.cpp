#include <ecal.hpp>
#include <debug.hpp>

//#define USE_RPC

/**
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

    if (clusterConf != nullptr || myNodeConf != nullptr)
        d_warn("clusterConf & myNodeConf were already initialized, skip");
    else {
        clusterConf = new ClusterConfig(cmdConf->clusterConfigFile);
        
        auto myself = clusterConf->findMyself();
        if (myself.id >= 0)
            myNodeConf = new NodeConfig(myself);
        else {
            d_err("cannot find configuration of this node");
            exit(-1);
        }
    }

    allocTable = new BlockPool<BlockTy>();
    rpc = new RPCInterface();
    rdma = rpc->getRDMASocket();

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

#if 0
    /* Start recovery process if this is a recovery */
    if (cmdConf->recover) {
        d_warn("start data recovery...");

        int peerId = -1;
        for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
            peerId = (*clusterConf)[i].id;
            if (rpcInterface->isPeerAlive(peerId))
                break;
        }
        if (peerId == -1) {
            d_err("cannot find a valid peer!");
            return;
        }

        /* Get remote write buffer MR & size */
        Message request, response;
        request.type = Message::MESG_RECOVER_START;
        rpcInterface->rpcCall(peerId, &request, &response);
        int writeLogLen = response.data.size;
        size_t writeLogSize = writeLogLen * sizeof(uint64_t);

        /* Allocate local write log space */
        std::vector<uint64_t> blkNos;
        blkNos.resize(writeLogLen);
        uint64_t *localDst = blkNos.data();
        ibv_mr *localLogMR = rpcInterface->getRDMASocket()->allocMR(localDst, writeLogSize, IBV_ACCESS_LOCAL_WRITE);

        /* Perform RDMA read */
        ibv_send_wr wr;
        ibv_sge sge;
        ibv_wc wc[2];

        memset(&sge, 0, sizeof(ibv_sge));
        sge.addr = (uint64_t)localDst;
        sge.length = writeLogSize;
        sge.lkey = localLogMR->lkey;

        memset(&wr, 0, sizeof(ibv_send_wr));
        wr.wr_id = WRID(peerId, SP_REMOTE_WRLOG_READ);
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_READ;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = (uint64_t)response.data.mr.addr;
        wr.wr.rdma.rkey = response.data.mr.rkey;

        rpcInterface->getRDMASocket()->postSpecialSend(peerId, &wr);
        expectPositive(rpcInterface->getRDMASocket()->pollSendCompletion(wc));

        /* Retrieve & Recover */
        int pagePerRow = clusterConf->getClusterSize() / N;
        uint8_t *recoverSrc[K];
        int decodeIndex[K];
        for (int i = 0; i < writeLogLen; ++i) {
            uint64_t row = blkNos[i];
            uint64_t blockShift = getBlockShift(row);
            int startNodeId = (row % pagePerRow) * N;
            int selfId = -1;
            for (int i = 0, j = 0; i < K && j < N; ++j) {
                int idx = (j + startNodeId) % N;
                if (idx != myNodeConf->id && rpcInterface->isPeerAlive(peerId))
                    decodeIndex[i++] = j;
                else
                    selfId = j;
            }
            int taskCnt = 0;
            for (int i = 0; i < K; ++i) {
                int idx = (decodeIndex[i] + startNodeId) % N;
                uint8_t *base = rpcInterface->getRDMASocket()->getReadRegion(peerId);
                rpcInterface->remoteReadFrom(idx, blockShift, (uint64_t)base, BlockTy::size, i);
                recoverSrc[i] = base;
                ++taskCnt;
            }

            ibv_wc wc[2];
            while (taskCnt)
                taskCnt -= rpcInterface->getRDMASocket()->pollSendCompletion(wc);
            
            uint8_t decodeMatrix[1 * K];
            uint8_t invertMatrix[K * K];
            uint8_t b[K * K];

            for (int i = 0; i < K; ++i)
                memcpy(&b[K * i], &encodeMatrix[K * decodeIndex[i]], K);
            gf_invert_matrix(b, invertMatrix, K);
            
            if (selfId < K)
                memcpy(decodeMatrix, &invertMatrix[K * selfId], K);
            else
                for (int i = 0; i < K; ++i) {
                    int s = 0;
                    for (int j = 0; j < K; ++j)
                        s ^= gf_mul(invertMatrix[j * K + i], encodeMatrix[K * selfId + j]);
                    decodeMatrix[i] = s;
                }
            
            uint8_t gfTbls[K * P * 32];
            uint8_t *recoverOutput[1] = { reinterpret_cast<uint8_t *>(allocTable->at(row)) };
            ec_init_tables(K, 1, decodeMatrix, gfTbls);
            ec_encode_data(BlockTy::size, K, 1, gfTbls, recoverSrc, recoverOutput);

            for (int i = 0; i < K; ++i) {
                int idx = (decodeIndex[i] + startNodeId) % N;
                rpcInterface->getRDMASocket()->freeReadRegion(idx, recoverSrc[i]);
            }

        }

        /* Local & remote cleanup */
        ibv_dereg_mr(localLogMR);

        request.type = Message::MESG_RECOVER_END;
        rpcInterface->rpcCall(peerId, &request, &response);

        d_warn("finished data recovery! ECAL start.");
    }
#endif
}

ECAL::~ECAL()
{
    if (memConf) {
        delete memConf;
        memConf = nullptr;
    }
}

int readCount = 0, writeCount = 0;

void ECAL::readBlock(uint64_t index, ECAL::Page &page)
{
    int decodeIndex[K], errIndex[K];
    uint8_t *recoverSrc[N], *recoverOutput[N];

    page.index = index;
    memset(page.page.data, 0, Block4K::capacity);

    auto pos = getDataPos(index);
    int errs = 0;
    for (int i = 0, j = 0; i < K && j < N; ++j) {
        int peerId = (j + pos.startNodeId) % N;
        if (rdma->isPeerAlive(peerId)) 
            decodeIndex[i++] = j;
        else if (j < K) {
            errIndex[errs] = j;
            recoverOutput[errs++] = page.page.data + j * BlockTy::size;
        }
    }

#ifndef USE_RPC
    /* Read data blocks from remote (or self) */
    uint64_t blockShift = getBlockShift(pos.row);
    int taskCnt = 0;
    for (int i = 0; i < K; ++i) {
        int peerId = (decodeIndex[i] + pos.startNodeId) % N;
        if (peerId != myNodeConf->id) {
            uint8_t *base = rdma->getReadRegion(peerId);
            rdma->postRead(peerId, blockShift, (uint64_t)base, BlockTy::size, i);
            if (errno) d_warn("boom! at (read %d) (write %d)", readCount, writeCount);
            readCount++;
            recoverSrc[i] = base;
            ++taskCnt;
        }
        else
            recoverSrc[i] = reinterpret_cast<uint8_t *>(allocTable->at(pos.row));
    }

    ibv_wc wc[MAX_NODES];
    rdma->pollSendCompletion(wc, taskCnt);
#else
    static MemResponse resps[N];
    int respId = 0;

    uint64_t blockShift = getBlockShift(pos.row);
    for (int i = 0; i < K; ++i) {
        int peerId = (decodeIndex[i] + pos.startNodeId) % N;
        if (peerId != myNodeConf->id) {
            PureValueRequest req;
            req.value = blockShift;
            netif->rpcCall(peerId, ErpcType::ERPC_MEMREAD, req, resps[respId]);
            recoverSrc[i] = resps[respId].data;
            respId++;
        }
        else
            recoverSrc[i] = reinterpret_cast<uint8_t *>(allocTable->at(pos.row));
    }
#endif

    /* Copy intact data */
    for (int i = 0; i < K; ++i)
        if (decodeIndex[i] < K)
            memcpy(page.page.data + decodeIndex[i] * BlockTy::size, recoverSrc[i], BlockTy::size);

    if (errs == 0) {
#ifndef USE_RPC
        for (int i = 0; i < K; ++i) {
            int peerId = (decodeIndex[i] + pos.startNodeId) % N;
            if (peerId != myNodeConf->id)
                rdma->freeReadRegion(peerId, recoverSrc[i]);
        }
#endif
        return;
    }
    
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

#ifndef USE_RPC
    for (int i = 0; i < K; ++i) {
        int peerId = (decodeIndex[i] + pos.startNodeId) % N;
        if (peerId != myNodeConf->id)
            rdma->freeReadRegion(peerId, recoverSrc[i]);
    }
#endif
}

void ECAL::writeBlock(ECAL::Page &page)
{
    static uint8_t *data[K];
    
    for (int i = 0; i < K; ++i)
        data[i] = page.page.data + i * BlockTy::size;
    ec_encode_data(BlockTy::size, K, P, gfTables, data, parity);

    DataPosition pos = getDataPos(page.index);
    uint64_t blockShift = getBlockShift(pos.row);
    ibv_wc wc[2];
    for (int i = 0; i < N; ++i) {
        int peerId = (pos.startNodeId + i) % N;
        uint8_t *blk = (i < K ? data[i] : parity[i - K]);
        
        if (peerId == myNodeConf->id)
            memcpy(allocTable->at(pos.row), blk, BlockTy::size);
        else if (rdma->isPeerAlive(peerId)) {
#ifndef USE_RPC
            uint8_t *base = rdma->getWriteRegion(peerId);
            memcpy(base, blk, BlockTy::size);
            rdma->postWrite(peerId, blockShift, (uint64_t)base, BlockTy::size);
            rdma->pollSendCompletion(wc);
            rdma->freeWriteRegion(peerId, base);
            writeCount++;
#else
            MemRequest req;
            PureValueResponse resp;
            req.addr = blockShift;
            memcpy(req.data, blk, BlockTy::size);
            netif->rpcCall(peerId, ErpcType::ERPC_MEMWRITE, req, resp);
#endif
        }
    }
}
