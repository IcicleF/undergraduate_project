#include <signal.h>
#include <ecal.hpp>

std::mutex mut;
std::condition_variable ctrlCCond;
bool ctrlCPressed = false;
void CtrlCHandler(int sig)
{
    std::unique_lock<std::mutex> lock(mut);
    ctrlCPressed = true;
    ctrlCCond.notify_one();
}

DEFINE_MAIN_INFO();

int main(int argc, char **argv)
{
    std::unique_lock<std::mutex> lock(mut);
    signal(SIGINT, CtrlCHandler);
    COLLECT_MAIN_INFO();

    cmdConf = new CmdLineConfig();
    ECAL ecal;

    printf("DataServer: main thread sleep.");
    
    while (!ctrlCPressed)
        ctrlCCond.wait(lock);

    printf("DataServer: Ctrl-C, stopListenerAndJoin");

    ecal.getRPCInterface()->stopListenerAndJoin();

    return 0;
}
