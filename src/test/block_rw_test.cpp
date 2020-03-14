#include <cstdio>
#include <random>
#include <chrono>
#include <string>
#include <gflags/gflags.h>

#include <config.hpp>
#include <ecal.hpp>

using namespace std;
using namespace std::chrono;

static bool rwtypeValidator(const char *flagname, const std::string &value)
{
    if (value == "rand" || value == "seq")
        return true;
    printf("Invalid value for --%s: %s\n", flagname, value.c_str());
    return false;
}

DEFINE_int32(ntests, 1000, "Count of 4kB write/reads");
DEFINE_bool(cont_rw, false, "If set, do RDMA read immediately after write");
DEFINE_bool(read_poll, true, "If set, poll for read completions before reading");
DEFINE_string(rwtype, "rand", "The block R/W pattern (either 'rand' or 'seq')");
DEFINE_validator(rwtype, &rwtypeValidator);

int N;
bool contRW, readPoll;
bool rwRand;

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
    printf("- poll for RDMA reads:        %s\n", (readPoll = FLAGS_read_poll) ? "yes" : "no");
    printf("- r/w pattern:                %s\n", FLAGS_rwtype);
    printf("\n");
    
    COLLECT_MAIN_INFO();
    ECAL ecal();

    static constexpr int selIndex = 1234;
    std::unordered_map<uint64_t, uint8_t> selData;

    // TODO

    return 0;
}