#include <cstdio>
#include <random>
#include <chrono>
#include <string>
#include <gflags/gflags.h>

#include <config.hpp>
#include <ecal.hpp>

using namespace std;
using namespace std::chrono;

#define MAXN 1000000

DEFINE_MAIN_INFO()

static bool rwtypeValidator(const char *flagname, const std::string &value)
{
    if (value == "rand" || value == "seq")
        return true;
    printf("Invalid value for --%s: %s\n", flagname, value.c_str());
    return false;
}

DEFINE_int32(ntests, 1000, "Count of 4kB write/reads");
DEFINE_bool(cont_rw, false, "If set, do RDMA read immediately after write");
DEFINE_string(rwtype, "rand", "The block R/W pattern (either 'rand' or 'seq')");
DEFINE_validator(rwtype, &rwtypeValidator);

int N;
bool contRW, readPoll;
bool rwRand;

uint32_t queue[MAXN + 5];
int front = 0, rear = 0;

int main(int argc, char **argv)
{
    gflags::SetUsageMessage("Galois 4kB-Block R/W Test Utility");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    rwRand = (FLAGS_rwtype == "rand");

    printf("\n");
    printf("***** Galois 4kB-Block R/W Test Utility ****\n");
    printf("\n");
    printf("Command Line options:\n");
    printf("- num of tests:               %d\n", (N = FLAGS_ntests));
    printf("- immediate read after write: %s\n", (contRW = FLAGS_cont_rw) ? "yes" : "no");
    printf("- r/w pattern:                %s\n", FLAGS_rwtype.c_str());
    printf("\n");
    
    if (N > MAXN) {
        printf("\033[31m" "ERROR: N > MAXN (%d)" "\033[0m\n", MAXN);
        printf("Quit.\n");
        printf("\n");
        return -1;
    }

    COLLECT_MAIN_INFO();
    
    cmdConf = new CmdLineConfig();
    ECAL ecal;

    if (myNodeConf->id != 0) {
        printf("This node is not #0, wait...");
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
    
    if (contRW)
        printf("Starting W/R/Degraded-R...\n");
    else
        printf("Starting write...\n");

    ECAL::Page page;
    for (int i = 0; i < N; ++i) {
        uint64_t blkid = (rwRand ? rnd(core) : i);
        page.index = blkid;
        for (int j = 0; j < Block4K::size; ++j)
            page.page.data[j] = rdat(core);
        pageIds.push_back(blkid);
        uint8_t sel = selData[blkid] = page.page.data[selIndex];

        auto start = steady_clock::now();
        ecal.writeBlock(page);
        auto end = steady_clock::now();
        writeUs += duration_cast<microseconds>(end - start).count();

        if (contRW) {
            start = steady_clock::now();
            ecal.readBlock(blkid, page);
            end = steady_clock::now();
            readUs += duration_cast<microseconds>(end - start).count();

            if (page.page.data[selIndex] == sel)
                ++readCorrect;

            ecal.getRPCInterface()->__markAsDead(1);
            start = steady_clock::now();
            ecal.readBlock(blkid, page);
            end = steady_clock::now();
            degradedReadUs += duration_cast<microseconds>(end - start).count();

            if (page.page.data[selIndex] == sel)
                ++degradedReadCorrect;
            ecal.getRPCInterface()->__cancelMarking(1);
        }
    }

    if (!contRW) {
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

        /* Degraded read */
        printf("Starting degraded read...");
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
    }

    printf("Test finished.\n");
    printf("\n");
    printf("Write: total %ld us, avg. %.3lf us\n", writeUs, (double)writeUs / N);
    printf("Read: total %ld us, avg. %.3lf us\n", readUs, (double)readUs / N);
    printf("Degraded Read: total %ld us, avg. %.3lf us\n", degradedReadUs, (double)degradedReadUs / N);
    printf("\n");

    ecal.getRPCInterface()->syncAmongPeers();
    delete cmdConf;
    return 0;
}
