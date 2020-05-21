#include <fs/DMStore.h>
#include <config.hpp>
#include <ecal.hpp>
#include <debug.hpp>
#include <network/netif.hpp>

#include <signal.h>

class DMServer
{
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

void dmHandleTest(erpc::ReqHandle *reqHandle, void *context)
{
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    resp->value = 0;
    sendResponse(reqHandle, context);
}
void dmHandleAccess(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    DirectoryInode di;
    resp->value = DMServer::getInstance()->access(req->path, di);
    sendResponse(reqHandle, context);
}
void dmHandleStat(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<StatResponse>(reqHandle, context);
    DirectoryInode di, di2;
    DMServer::getInstance()->getAttr(di, req->path, di2);
    if ((resp->result = di.error) == 0) {
        resp->dirStat.uuid = di.uuid;
        resp->dirStat.st.st_mtime = 0;
        resp->dirStat.st.st_atime = 0;
        resp->dirStat.st.st_uid = di.uid;
        resp->dirStat.st.st_gid = di.gid;
        resp->dirStat.st.st_mode = di.mode;
        resp->dirStat.st.st_ctime = di.ctime;
    }
    sendResponse(reqHandle, context);
}
void dmHandleMkdir(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    DirectoryInode di;
    di.mode = req->value;
    di.uid = di.gid = 0777;
    resp->value = DMServer::getInstance()->mkdir(req->path, di);
    sendResponse(reqHandle, context);
}
void dmHandleRmdir(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    DirectoryInode di;
    resp->value = DMServer::getInstance()->rmdir(req->path, di);
    sendResponse(reqHandle, context);
}
void dmHandleReaddir(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<RawResponse>(reqHandle, context);
    DirectoryInode di;
    std::string result;
    DMServer::getInstance()->readdir(result, req->path);
    strncpy(reinterpret_cast<char *>(resp->raw), result.c_str(), RawResponse::RAW_SIZE);
    resp->raw[RawResponse::RAW_SIZE] = 0;
    sendResponse(reqHandle, context);
}

NetworkInterface *netif;
void CtrlCHandler(int sig)
{
    if (netif)
        netif->stopServer();
}

DEFINE_MAIN_INFO();

int main(int argc, char **argv)
{
#if 1
    signal(SIGINT, CtrlCHandler);
    COLLECT_MAIN_INFO();

    DMServer::getInstance();
    cmdConf = new CmdLineConfig();

    ECAL ecal;

    /* Initialize RPC engine */
    std::unordered_map<int, erpc::erpc_req_func_t> reqFuncs;
    reqFuncs[static_cast<int>(ErpcType::ERPC_TEST)] = dmHandleTest;
    reqFuncs[static_cast<int>(ErpcType::ERPC_ACCESS)] = dmHandleAccess;
    reqFuncs[static_cast<int>(ErpcType::ERPC_DIRSTAT)] = dmHandleStat;
    reqFuncs[static_cast<int>(ErpcType::ERPC_MKDIR)] = dmHandleMkdir;
    reqFuncs[static_cast<int>(ErpcType::ERPC_RMDIR)] = dmHandleRmdir;
    reqFuncs[static_cast<int>(ErpcType::ERPC_READDIR)] = dmHandleReaddir;
    
    netif = new NetworkInterface(reqFuncs);
    ecal.regNetif(netif);

    printf("DMServer: ECAL constructed & exited ctor.\n");
    fflush(stdout);
    
    printf("DMServer: main thread sleep.\n");
    fflush(stdout);

    netif->startServer();

    printf("DMServer: Ctrl-C, stopListenerAndJoin\n");
    fflush(stdout);

    ecal.getRDMASocket()->stopListenerAndJoin();
#else
    cmdConf = new CmdLineConfig;
    memConf = new MemoryConfig(*cmdConf);
    clusterConf = new ClusterConfig(cmdConf->clusterConfigFile);
    auto myself = clusterConf->findMyself();
    myNodeConf = new NodeConfig(myself);
    
    NetworkInterface netif;
#endif

    return 0;
}
