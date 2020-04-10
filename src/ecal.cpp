#include <ecal.hpp>
#include <debug.hpp>

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
    
    allocTable = new BlockPool<BlockTy>();
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
        for (int i = 0; i < writeLogLen; ++i) {
            //TODO
        }

        /* Local & remote cleanup */
        ibv_dereg_mr(localLogMR);

        d_warn("finished data recovery! ECAL start.");
    }
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
            rpcInterface->remoteWriteTo(peerId, blockShift, (uint64_t)base, BlockTy::size, pos.row);
        }
    }
}
