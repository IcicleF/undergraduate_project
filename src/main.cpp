#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>

#include <config.hpp>
#include <ecal.hpp>

#define TESTS  1000

using namespace std;
using std::chrono::microseconds;

long wrdmaus = 0, rrdmaus = 0;
long wisalus = 0, risalus = 0;

int main(int argc, char **argv)
{
    isRunning.store(true);

    cmdConf = new CmdLineConfig();
    cmdConf->setAsDefault();

    ECAL *ecal = new ECAL();
    uint64_t maxIndex = ecal->getClusterCapacity();

    srand(time(0));

    d_info("start main");

    if (myNodeConf->id == 0) {
        d_warn("start waiting...");
    
        while (!ecal->ready());

        printf("ready! wait 2s before test...\n");

        usleep(2 * 1000 * 1000);

        long readus = 0, writeus = 0;

        int indexes[TESTS + 5];
        uint8_t bytes[TESTS + 5];
        for (int i = 0; i < TESTS; ++i) {
            int index = rand() % maxIndex;
            indexes[i] = index;

            ECAL::Page page(index);
            for (int j = 0; j < Block4K::size; j++)
                page.page.data[j] = rand() % 256;
            bytes[i] = page.page.data[1111];

            auto start = chrono::steady_clock::now();
            ecal->writeBlock(page);
            auto end = chrono::steady_clock::now();
            writeus += chrono::duration_cast<microseconds>(end-start).count();
            usleep(1000);
        }

        d_info("survived write!");

        int errs = 0;
        for (int i = 0; i < TESTS; ++i) {
            auto start = chrono::steady_clock::now();
            ECAL::Page page = ecal->readBlock(indexes[i]);
            auto end = chrono::steady_clock::now();
            readus += chrono::duration_cast<microseconds>(end-start).count();

            if (page.page.data[1111] != bytes[i])
                ++errs;
        }

        d_info("survived read!");

        printf("Correct: %d, Wrong: %d\n", TESTS - errs, errs);
        printf("Read: %.3lf us, Write: %.3lf us\n", (double)readus / TESTS, (double)writeus / TESTS);
        
        printf("R: EC: %.3lf us, RDMA: %.3lfus\n", (double)risalus / TESTS, (double)rrdmaus / TESTS);
        printf("W: EC: %.3lf us, RDMA: %.3lfus\n", (double)wisalus / TESTS, (double)wrdmaus / TESTS);
        
        fflush(stdout);

        usleep(1000 * 1000);
    }

    ecal->stop();           /* Synchronously stop */
    usleep(1000 * 1000);

    delete ecal;
    delete cmdConf;

    return 0;
}
