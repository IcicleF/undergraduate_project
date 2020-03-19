#include <cstdio>
#include <random>
#include <chrono>
#include <string>
#include <gflags/gflags.h>
#include <isa-l.h>

#include <config.hpp>
#include <rpc.hpp>

using namespace std;
using namespace std::chrono;

#define MAXN 1000000

struct Page
{
    Block4K page;
    uint64_t index = 0;

    Page(uint64_t index = 0) : index(index) { }
};

class ECAL2
{
public:
    static const int K = 2;
    static const int P = 1;
    static const int N = K + P;

public:
    explicit ECAL2()
    {
        if (cmdConf == nullptr) {
            d_err("cmdConf should be initialized!");
            exit(-1);
        }
        if (memConf != nullptr)
            d_warn("memConf is already initialized, skip");
        else
            memConf = new MemoryConfig(*cmdConf);
        
        allocTable = new AllocationTable<Block4K>();
        rpcInterface = new RPCInterface();

        if (clusterConf->getClusterSize() != N) {
            d_err("FIXME: clusterSize != N, exit");
            exit(-1);
        }

        /* Compute capacity */
        int clusterNodeCount = clusterConf->getClusterSize();
        capacity = (clusterNodeCount / N * K * allocTable->getCapacity());

        /* Initialize EC Matrices */
        gf_gen_cauchy1_matrix(encodeMatrix, N, K);
        ec_init_tables(K, P, &encodeMatrix[K * K], gfTables);

        for (int i = 0; i < P; ++i)
            parity[i] = encodeBuffer + i * Block4K::size;
    }
    ~ECAL2()
    {
        if (memConf)
            delete memConf;
    }

    RPCInterface *getRPCInterface() const { return rpcInterface; }
    uint64_t getClusterCapacity() const { return capacity; }

    void readBlock(uint64_t index, Page &page)
    {
        auto pos = getBlockPosition(index);
        page.index = index;
        uint64_t blockShift = allocTable->getShift(pos.row);

        if (rpcInterface->isPeerAlive(pos.nodeId)) {
            if (pos.nodeId == myNodeConf->id) {
                memcpy(page.page.data, allocTable->at(pos.row), Block4K::size);
                return;
            }
            uint8_t *base = rpcInterface->getRDMASocket()->getReadRegion(pos.nodeId);
            rpcInterface->remoteReadFrom(pos.nodeId, blockShift, (uint64_t)base, Block4K::size);
            
            ibv_wc wc[2];
            rpcInterface->getRDMASocket()->pollSendCompletion(wc);
            if (wc->status != IBV_WC_SUCCESS) {
                d_err("wc failed: %d", wc->status);
                exit(-1);
            }
            memcpy(page.page.data, base, Block4K::size);
            return;
        }

        int decodeIndex[K];
        uint8_t *recoverSrc[N];

        int shift = pos.row % N;
        int offset = (pos.nodeId - shift + N) % N;
        for (int i = 0, j = 0; i < K && j < N; ++j) {
            int peerId = (shift + j) % N;
            if (rpcInterface->isPeerAlive(peerId)) {
                decodeIndex[i++] = j;
                if (peerId != myNodeConf->id) {
                    uint8_t *base = rpcInterface->getRDMASocket()->getReadRegion(peerId);
                    rpcInterface->remoteReadFrom(peerId, blockShift, (uint64_t)base, Block4K::size, i);
                    recoverSrc[i] = base;
                }
                else
                    recoverSrc[i] = reinterpret_cast<uint8_t *>(allocTable->at(pos.row));
            }
        }

        uint8_t invertMatrix[N * K];
        uint8_t b[K * K];

        for (int i = 0; i < K; ++i)
            memcpy(&b[K * i], &encodeMatrix[K * decodeIndex[i]], K);
        if (gf_invert_matrix(b, invertMatrix, K) < 0) {
            d_err("cannot do matrix invert!");
            return;
        }
        
        uint8_t gfTbls[K * 32];
        uint8_t *output[1] = { page.page.data };
        ec_init_tables(K, 1, &invertMatrix[K * offset], gfTbls);
        ec_encode_data(Block4K::size, K, 1, gfTbls, recoverSrc, output);
    }
    void writeBlock(Page &page)
    {
        auto pos = getBlockPosition(page.index);
        uint64_t blockShift = allocTable->getShift(pos.row);
        int shift = pos.row % N;

        uint8_t *encodeSrc[K];
        for (int i = 0; i < K; ++i) {
            int peerId = (shift + i) % N;
            if (peerId == myNodeConf->id)
                encodeSrc[i] = reinterpret_cast<uint8_t *>(allocTable->at(pos.row));
            else {
                uint8_t *base = rpcInterface->getRDMASocket()->getReadRegion(peerId);
                rpcInterface->remoteReadFrom(peerId, blockShift, (uint64_t)base, Block4K::size, i);
                encodeSrc[i] = base;
            }
        }

        ec_encode_data(Block4K::size, K, P, gfTables, encodeSrc, parity);
        
        for (int i = 0; i < P; ++i) {
            int peerId = (shift + i + K) % N;
            if (peerId == myNodeConf->id)
                memcpy(allocTable->at(pos.row), parity[i], Block4K::size);
            else if (rpcInterface->isPeerAlive(peerId)) {
                uint8_t *base = rpcInterface->getRDMASocket()->getWriteRegion(peerId);
                memcpy(base, parity[i], Block4K::size);
                rpcInterface->remoteWriteTo(peerId, blockShift, (uint64_t)base, Block4K::size);
            }
            else
                d_err("peer %d is dead, parity write failed.", peerId);
        }
    }

private:
    AllocationTable<Block4K> *allocTable = nullptr;
    RPCInterface *rpcInterface = nullptr;
    uint64_t capacity = 0;

    uint8_t encodeMatrix[N * K];
    uint8_t gfTables[K * P * 32];
    uint8_t encodeBuffer[P * Block4K::capacity];
    uint8_t *parity[P];                             /* Points to encodeBuffer */

    struct DataPosition
    {
        uint64_t row;
        int nodeId;

        DataPosition(uint64_t row = 0, int nodeId = -1) : row(row), nodeId(nodeId) { }
        DataPosition(const DataPosition &b) : row(b.row), nodeId(b.nodeId) { }
    };

    DataPosition getBlockPosition(uint64_t index)
    {
        uint64_t row = index / N;
        int nodeId = index % K;
        nodeId = (nodeId + (row % N)) % N;
        return DataPosition(row, nodeId);
    }
};

DEFINE_MAIN_INFO()

static bool rwtypeValidator(const char *flagname, const std::string &value)
{
    if (value == "rand" || value == "seq")
        return true;
    printf("Invalid value for --%s: %s\n", flagname, value.c_str());
    return false;
}

DEFINE_int32(ntests, 1000, "Count of 4kB write/reads");
DEFINE_string(rwtype, "rand", "The block R/W pattern (either 'rand' or 'seq')");
DEFINE_validator(rwtype, &rwtypeValidator);

int N;
bool rwRand;

int main(int argc, char **argv)
{
    gflags::SetUsageMessage("Galois 4kB-Block R/W Test Utility (EC type 2)");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    rwRand = (FLAGS_rwtype == "rand");

    printf("\n");
    printf("***** Galois 4kB-Block R/W Test Utility ****\n");
    printf("\n");
    printf("Command Line options:\n");
    printf("- num of tests:   %d\n", (N = FLAGS_ntests));
    printf("- r/w pattern:    %s\n", FLAGS_rwtype.c_str());
    printf("\n");
    
    if (N > MAXN) {
        printf("\033[31m" "ERROR: N > MAXN (%d)" "\033[0m\n", MAXN);
        printf("Quit.\n");
        printf("\n");
        return -1;
    }

    COLLECT_MAIN_INFO();

    cmdConf = new CmdLineConfig();
    ECAL2 ecal;

    if (myNodeConf->id != 0) {
        printf("This node is not #0, wait...\n");
        ecal.getRPCInterface()->syncAmongPeers();
        return 0;
    }

    static constexpr int selIndex = 1234;
    unordered_map<uint64_t, uint8_t> selData;

    vector<uint64_t> pageIds;
    uint64_t cap = ecal.getClusterCapacity();
    mt19937 core;
    uniform_int_distribution<uint64_t> rnd(0, cap - 1);
    uniform_int_distribution<uint8_t> rdat(0, 255);

    long writeUs = 0, readUs = 0, degradedReadUs = 0;
    int readCorrect = 0, degradedReadCorrect = 0;
    Page page;

    /* Write */
    printf("Starting write...\n");
    for (int i = 0; i < N; ++i) {
        uint64_t blkid = (rwRand ? rnd(core) : i);
        page.index = blkid;
        for (int j = 0; j < Block4K::size; ++j)
            page.page.data[j] = rdat(core);
        pageIds.push_back(blkid);
        selData[blkid] = page.page.data[selIndex];

        auto start = steady_clock::now();
        ecal.writeBlock(page);
        auto end = steady_clock::now();
        writeUs += duration_cast<microseconds>(end - start).count();
    }

    /* Normal read */
    printf("Starting read...\n");
    for (int i = 0; i < N; ++i) {
        uint64_t blkid = pageIds[i];
        uint8_t sel = selData[blkid];

        auto start = steady_clock::now();
        ecal.readBlock(blkid, page);
        auto end = steady_clock::now();
        readUs += duration_cast<microseconds>(end - start).count();

        if (page.page.data[selIndex] == sel)
            ++readCorrect;
    }

    /* Randomly-degraded read */
    printf("Starting degraded read...\n");
    ecal.getRPCInterface()->__markAsDead(1);
    for (int i = 0; i < N; ++i) {
        uint64_t blkid = pageIds[i];
        uint8_t sel = selData[blkid];

        auto start = steady_clock::now();
        ecal.readBlock(blkid, page);
        auto end = steady_clock::now();
        degradedReadUs += duration_cast<microseconds>(end - start).count();

        if (page.page.data[selIndex] == sel)
            ++degradedReadCorrect;
    }

    printf("Test finished.\n");
    printf("\n");
    printf("Write: total %ld us, avg. %.3lf us\n", writeUs, (double)writeUs / N);
    printf("Read: %d/%d correct, total %ld us, avg. %.3lf us\n", readCorrect, N,
            readUs, (double)readUs / N);
    printf("Degraded Read: %d/%d correct, total %ld us, avg. %.3lf us\n", degradedReadCorrect, N,
            degradedReadUs, (double)degradedReadUs / N);
    printf("\n");

    ecal.getRPCInterface()->syncAmongPeers();
    delete cmdConf;

    return 0;
}