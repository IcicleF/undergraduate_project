#include <fs/DMStore.h>
#include <config.hpp>
#include <ecal.hpp>
#include <debug.hpp>
#include <network/msg.hpp>

#include <signal.h>

class DMServer
{
    friend void processDMRPC(RpcType, const void *, void *, int *);

public:
    static DMStore *getInstance()
    {
        static DMServer *inst = nullptr;
        if (!inst)
            inst = new DMServer();
        return inst->dm;
    }

private:
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

    DMStore *dm = nullptr;
};

void processDMRPC(RpcType reqType, const void *request, void *response, int *respSize)
{
    auto *dms = DMServer::getInstance();
    DirectoryInode di, di2;
    
    switch (reqType) {
        case RpcType::RPC_TEST: {
            PREPARE_RPC(req, PureValueRequest, request, resp, PureValueResponse, response, respSize);
            printf("Memory[0..10] = ");
            for (int i = 0; i < 10; ++i)
                printf("%d ", *(reinterpret_cast<char *>(memConf->getMemory()) + i));
            printf("\n");
            resp->value = myNodeConf->id;
            break;
        }
        case RpcType::RPC_ACCESS: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, PureValueResponse, response, respSize);
            resp->value = dms->access(req->path, di);
            break;
        }
        case RpcType::RPC_DIRSTAT: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, StatResponse, response, respSize);
            dms->getAttr(di, req->path, di2);
            if ((resp->result = di.error) == 0) {
                resp->dirStat.uuid = di.uuid;
                resp->dirStat.st.st_mtime = 0;
                resp->dirStat.st.st_atime = 0;
                resp->dirStat.st.st_uid = di.uid;
                resp->dirStat.st.st_gid = di.gid;
                resp->dirStat.st.st_mode = di.mode;
                resp->dirStat.st.st_ctime = di.ctime;
            }
            break;
        }
        case RpcType::RPC_MKDIR: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, PureValueResponse, response, respSize);
            di.mode = req->value;
            di.uid = di.gid = 0777;
            resp->value = dms->mkdir(req->path, di);
            break;
        }
        case RpcType::RPC_RMDIR: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, PureValueResponse, response, respSize);
            resp->value = dms->rmdir(req->path, di);
            break;
        }
        case RpcType::RPC_READDIR: {
            PREPARE_RPC(req, ValueWithPathRequest, request, resp, RawResponse, response, respSize);
            std::string result;
            dms->readdir(result, req->path);

            resp->len = result.length();
            strncpy(reinterpret_cast<char *>(resp->raw), result.c_str(), RawResponse::RAW_SIZE);
            resp->raw[RawResponse::RAW_SIZE] = 0;
            break;
        }
        default: {
            *respSize = 0;
            break;
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

    rpc->registerRPCProcessor(processDMRPC);
    rpc->server();

    printf("DMServer: stop\n");

    ecal.getRDMASocket()->stopListenerAndJoin();

    return 0;
}