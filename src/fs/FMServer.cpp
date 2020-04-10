#include <fs/FMStore.h>
#include <config.hpp>
#include <ecal.hpp>

#include <signal.h>

class FMServer
{
    friend void processFMRPC(const RPCMessage *, RPCMessage *);

public:
    static FMServer *getInstance()
    {
        static FMServer *inst = nullptr;
        if (!inst)
            inst = new FMServer();
        return inst;
    }

private:
    FMStore *fm;

    FMServer()
    {
        int id = myNodeConf ? myNodeConf->id : 233;
        fm = new FMStore(id);
    }
};

void processFMRPC(const RPCMessage *request, RPCMessage *response)
{
    using std::string;

    auto *fms = FMServer::getInstance();
    FileAccessInode fai;
    FileContentInode fci;
    FileInode fi, fi2;
    response->type = RPCMessage::RPC_UNDEF;
    
    switch (request->type) {
        case RPCMessage::RPC_TEST: {
            response->result = 0;
            break;
        }
        case RPCMessage::RPC_CREATE: {
            string path = request->path;        
            fai.mode = request->mode;
            fai.uid = fai.gid = 0777;
            response->result = fms->fm->create(path, fai);
            break;
        }
        case RPCMessage::RPC_ACCESS: {
            string path = request->path;
            response->result = fms->fm->access(path, fai);
            break;
        }
        case RPCMessage::RPC_CSIZE: {
            string path = request->path;
            auto *pfci = reinterpret_cast<const FileContentInode *>(request->raw2);
            response->result = fms->fm->csize(path, *pfci);
            break;
        }
        case RPCMessage::RPC_STAT: {
            string path = request->path;
            loco_file_stat st;
            fms->fm->getAttr(fi, path, fi2);
            if ((response->result = fi.error) == 0) {
                st.st.st_mode = S_IFREG | 0744;
                st.st.st_uid = fi.fa.uid;
                st.st.st_gid = fi.fa.gid;
                st.st.st_ctime = fi.fa.ctime;
                st.st.st_mtime = fi.fc.mtime;
                st.st.st_atime = fi.fc.mtime;
                st.st.st_size = fi.fc.size;
                st.sid = fi.fc.sid;
                st.suuid = fi.fc.suuid;
                st.block_size = fi.fc.block_size;
                memcpy(response->raw, &st, sizeof(loco_file_stat));
            }
	    break;
        }
        case RPCMessage::RPC_OPEN: {
            string path = request->path;
            fms->fm->open(fi, path, fai);
            memcpy(response->raw, &fi, sizeof(FileInode));
            break;
        }
        case RPCMessage::RPC_READDIR: {
            int64_t uuid = (int64_t)request->raw64[0];
            string res;
            fms->fm->readdir(res, uuid);
            response->result = res.length();
            strncpy(reinterpret_cast<char *>(response->raw), res.c_str(), MAX_READDIR_LEN);
            response->raw[MAX_READDIR_LEN] = 0;
            break;
        }
        case RPCMessage::RPC_REMOVE: {
            string path = request->path;
            fms->fm->remove(path, fai);
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

    FMServer::getInstance();
    cmdConf = new CmdLineConfig();
    ECAL ecal;
    ecal.getRPCInterface()->registerRPCProcessor(processFMRPC);
    
    printf("FMServer: main thread sleep.\n");
    fflush(stdout);

    while (!ctrlCPressed)
        ctrlCCond.wait(lock);

    printf("FMServer: Ctrl-C, stopListenerAndJoin\n");
    fflush(stdout);

    ecal.getRPCInterface()->stopListenerAndJoin();

    return 0;
}
