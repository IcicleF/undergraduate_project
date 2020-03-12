#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <config.hpp>
#include <ecal.hpp>

#define TESTS  1

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
        while (!ecal->ready());

        printf("ready! wait 2s before test...");

        usleep(2 * 1000 * 1000);

        int indexes[TESTS + 5];
        uint8_t bytes[TESTS + 5];
        for (int i = 0; i < TESTS; ++i) {
            int index = rand() % maxIndex;
            indexes[i] = index;

            ECAL::Page page(index);
            for (int j = 0; j < Block4K::size; j++)
                page.page.data[j] = rand() % 256;
            bytes[i] = page.page.data[1111];

            ecal->writeBlock(page);
        }

        d_info("survived write!");

        int errs = 0;
        for (int i = 0; i < TESTS; ++i) {
            ECAL::Page page = ecal->readBlock(indexes[i]);
            if (page.page.data[1111] != bytes[i])
                ++errs;
        }

        d_info("survived read!");

        printf("Correct: %d, Wrong: %d\n", TESTS - errs, errs);
    }
    else
        while (1);      /* Loop forever */

    delete ecal;
    delete cmdConf;

    return 0;
}
