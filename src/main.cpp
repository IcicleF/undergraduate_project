#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <config.hpp>
#include <ecal.hpp>

int main(int argc, char **argv)
{
    isRunning.store(true);

    cmdConf = new CmdLineConfig();
    cmdConf->setAsDefault();

    ECAL *ecal = new ECAL();
    uint64_t maxIndex = ecal->getClusterCapacity();

    srand(time(0));

    d_info("start main");
    return -1;

    int indexes[10005];
    uint8_t bytes[10005];
    for (int i = 0; i < 10000; ++i) {
        int index = rand() % maxIndex;
        indexes[i] = index;

        ECAL::Page page(index);
        for (int j = 0; j < Block4K::size; j++)
            page.page.data[j] = rand() % 256;
        bytes[i] = page.page.data[1111];

        ecal->writeBlock(page);
    }

    int errs = 0;
    for (int i = 0; i < 10000; ++i) {
        ECAL::Page page = ecal->readBlock(indexes[i]);
        if (page.page.data[1111] != bytes[i])
            ++errs;
    }

    printf("Correct: %d, Wrong: %d\n", 10000 - errs, errs);

    delete ecal;
    delete cmdConf;

    return 0;
}
