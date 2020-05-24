#include <fs/FMStore.h>
#include <config.hpp>
#include <ecal.hpp>
#include <debug.hpp>
#include <network/msg.hpp>

#include <signal.h>

class FMServer
{
    friend void processFMRPC(RpcType, const void *, void *, int *);

public:
    static FMStore *getInstance()
    {
        static FMServer *inst = nullptr;
        if (!inst)
            inst = new FMServer();
        return inst->fm;
    }

private:
    FMServer()
    {
        int id = myNodeConf ? myNodeConf->id : 233;
        fm = new FMStore(id);
    }

    FMStore *fm = nullptr;
};

void processFMRPC(RpcType reqType, const void *request, void *response, int *respSize)
{
    auto *fms = FMServer::getInstance();
    FileAccessInode fai;
    FileContentInode fci;
    FileInode fi, fi2;
    
    switch (reqType) {
        case RpcType::RPC_TEST: {
            PREPARE_RPC(req, PureValueRequest, request, resp, PureValueResponse, response, respSize);
            resp->value = myNodeConf->id;
            break;
        }
        case RpcType::RPC_CREATE: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, PureValueResponse, response, respSize);
            fai.mode = req->value;
            fai.uid = fai.gid = 0777;
            resp->value = fms->create(req->path, fai);
            break;
        }
        case RpcType::RPC_REMOVE: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, PureValueResponse, response, respSize);
            resp->value = fms->remove(req->path, fai);
            break;
        }
        case RpcType::RPC_ACCESS: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, PureValueResponse, response, respSize);
            resp->value = fms->access(req->path, fai);
            break;
        }
        case RpcType::RPC_CSIZE: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, PureValueResponse, response, respSize);
            fci.size = req->value;
            resp->value = fms->csize(req->path, fci);
            break;
        }
        case RpcType::RPC_FILESTAT: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, StatResponse, response, respSize);
            fms->getAttr(fi, req->path, fi2);
            if ((resp->result = fi.error) == 0) {
                resp->fileStat.st.st_mode = S_IFREG | 0744;
                resp->fileStat.st.st_uid = fi.fa.uid;
                resp->fileStat.st.st_gid = fi.fa.gid;
                resp->fileStat.st.st_ctime = fi.fa.ctime;
                resp->fileStat.st.st_mtime = fi.fc.mtime;
                resp->fileStat.st.st_atime = fi.fc.mtime;
                resp->fileStat.st.st_size = fi.fc.size;
                resp->fileStat.sid = fi.fc.sid;
                resp->fileStat.suuid = fi.fc.suuid;
                resp->fileStat.block_size = fi.fc.block_size;
            }
            break;
        }
        case RpcType::RPC_OPEN: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, InodeResponse, response, respSize);
            fms->open(resp->fi, req->path, fai);
            break;
        }
        case RpcType::RPC_READDIR: {
            PREPARE_RPC(req, PureValueRequest, request, resp, RawResponse, response, respSize);
            std::string result;
            fms->readdir(result, req->value);
            resp->len = result.length();
            strncpy(reinterpret_cast<char *>(resp->raw), result.c_str(), RawResponse::RAW_SIZE);
            resp->raw[RawResponse::RAW_SIZE] = 0;
            break;
        }
        default: {
            *respSize = 0;
            return;
        }
    }
}

RPCInterface *rpc;
void CtrlCHandler(int sig)
{
    d_warn("Ctrl-C");
    rpc->stop();
}

DEFINE_MAIN_INFO();

int main(int argc, char **argv)
{
    signal(SIGINT, CtrlCHandler);
    COLLECT_MAIN_INFO();

    cmdConf = new CmdLineConfig();
    ECAL ecal;
    rpc = ecal.getRPCInterface();

    rpc->registerRPCProcessor(processFMRPC);
    rpc->server();

    printf("FMServer: stop\n");

    ecal.getRDMASocket()->stopListenerAndJoin();

    return 0;
}
