#include <fs/DMStore.h>
#include <config.hpp>
#include <ecal.hpp>

#include <signal.h>

class DMServer
{
    friend void processDMRPC(const RPCMessage *, RPCMessage *);

public:
    static DMServer *getInstance()
    {
        static DMServer *inst = nullptr;
        if (!inst)
            inst = new DMServer();
        return inst;
    }

private:
    DMStore *dm;

    DMServer() : dm(new DMStore())
    {
        DirectoryInode dii;
        dii.mode = 0755;
        dii.ctime = time(NULL);
        dii.gid = 0777;
        dii.uid = 0777;
        dii.old_name = "/";

        dm->mkdir("/", dii);
    }
};

void processDMRPC(const RPCMessage *request, RPCMessage *response)
{
    using std::string;

    auto *dms = DMServer::getInstance();
    DirectoryInode di, di2;
    response->type = RPCMessage::RPC_UNDEF;
    
    switch (request->type) {
        case RPCMessage::RPC_ACCESS: {
            string path = request->path;
            response->result = dms->dm->access(path, di);
            break;
        }
        case RPCMessage::RPC_STAT: {
            string path = request->path;
            loco_dir_stat st;
            dms->dm->getAttr(di, path, di2);
            if ((response->result = di.error) == 0) {
                st.uuid = di.uuid;
                st.st.st_mtime = time(NULL);
                st.st.st_atime = time(NULL);
                st.st.st_uid = di.uid;
                st.st.st_gid = di.gid;
                st.st.st_mode = di.mode;
                st.st.st_ctime = di.ctime;
                memcpy(response->raw, &st, sizeof(loco_dir_stat));
            }
        }
        case RPCMessage::RPC_MKDIR: {
            string path = request->path;        
            di.mode = request->mode;
            di.uid = di.gid = 0777;
            response->result = dms->dm->mkdir(path, di);
            break;
        }
        case RPCMessage::RPC_RMDIR: {
            string path = request->path;
            response->result = dms->dm->rmdir(path, di);
            break;
        }
        case RPCMessage::RPC_OPENDIR: {
            string path = request->path;
            response->result = dms->dm->opendir(path, di);
            break;
        }
        case RPCMessage::RPC_READDIR: {
            string res, path = request->path;
            dms->dm->readdir(res, path);
            response->result = res.length();
            strncpy(reinterpret_cast<char *>(response->raw), res.c_str(), MAX_READDIR_LEN);
            response->raw[MAX_READDIR_LEN] = 0;
            break;
        }
        default: {
            d_err("unexpected RPC type: %d", (int)request->type);
            return;
        }
    }
}

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

    DMServer::getInstance();
    cmdConf = new CmdLineConfig();
    ECAL ecal;
    ecal.getRPCInterface()->registerRPCProcessor(processDMRPC);
    
    printf("DMServer: main thread sleep.");

    while (!ctrlCPressed)
        ctrlCCond.wait(lock);

    printf("DMServer: Ctrl-C, stopListenerAndJoin");

    ecal.getRPCInterface()->stopListenerAndJoin();

    return 0;
}
