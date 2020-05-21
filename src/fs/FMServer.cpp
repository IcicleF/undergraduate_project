#include <fs/FMStore.h>
#include <config.hpp>
#include <ecal.hpp>
#include <network/netif.hpp>

#include <signal.h>

class FMServer
{
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
    FMStore *fm;
};

void fmHandleTest(erpc::ReqHandle *reqHandle, void *context)
{
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    resp->value = 0;
    sendResponse(reqHandle, context);
}
void fmHandleAccess(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    FileAccessInode fai;
    resp->value = FMServer::getInstance()->access(req->path, fai);
    sendResponse(reqHandle, context);
}
void fmHandleCsize(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    FileContentInode fci;
    fci.size = req->value;
    resp->value = FMServer::getInstance()->csize(req->path, fci);
    sendResponse(reqHandle, context);
}
void fmHandleStat(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<StatResponse>(reqHandle, context);
    FileInode fi, fi2;
    FMServer::getInstance()->getAttr(fi, req->path, fi2);
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
    sendResponse(reqHandle, context);
}
void fmHandleOpen(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<InodeResponse>(reqHandle, context);
    FileAccessInode fai;
    FMServer::getInstance()->open(resp->fi, req->path, fai);
    sendResponse(reqHandle, context);
}
void fmHandleCreate(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    FileAccessInode fai;
    fai.mode = req->value;
    fai.uid = fai.gid = 0777;
    resp->value = FMServer::getInstance()->create(req->path, fai);
    sendResponse(reqHandle, context);
}
void fmHandleRemove(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<ValueWithPathRequest>(reqHandle);
    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    FileAccessInode fai;
    resp->value = FMServer::getInstance()->remove(req->path, fai);
    sendResponse(reqHandle, context);
}
void fmHandleReaddir(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<PureValueRequest>(reqHandle);
    auto *resp = allocateResponse<RawResponse>(reqHandle, context);
    std::string result;
    FMServer::getInstance()->readdir(result, req->value);
    resp->len = result.length();
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
    signal(SIGINT, CtrlCHandler);
    COLLECT_MAIN_INFO();

    FMServer::getInstance();
    cmdConf = new CmdLineConfig();
    ECAL ecal;

    /* Initialize RPC engine */
    std::unordered_map<int, erpc::erpc_req_func_t> reqFuncs;
    reqFuncs[static_cast<int>(ErpcType::ERPC_TEST)] = fmHandleTest;
    reqFuncs[static_cast<int>(ErpcType::ERPC_ACCESS)] = fmHandleAccess;
    reqFuncs[static_cast<int>(ErpcType::ERPC_CSIZE)] = fmHandleCsize;
    reqFuncs[static_cast<int>(ErpcType::ERPC_FILESTAT)] = fmHandleStat;
    reqFuncs[static_cast<int>(ErpcType::ERPC_CREATE)] = fmHandleCreate;
    reqFuncs[static_cast<int>(ErpcType::ERPC_REMOVE)] = fmHandleRemove;
    reqFuncs[static_cast<int>(ErpcType::ERPC_OPEN)] = fmHandleOpen;
    reqFuncs[static_cast<int>(ErpcType::ERPC_READDIR)] = fmHandleReaddir;

    netif = new NetworkInterface(reqFuncs);
    ecal.regNetif(netif);

    printf("FMServer: ECAL constructed & exited ctor.\n");
    fflush(stdout);
    
    printf("FMServer: main thread sleep.\n");
    fflush(stdout);

    netif->startServer();

    printf("FMServer: Ctrl-C, stopListenerAndJoin\n");
    fflush(stdout);

    ecal.getRDMASocket()->stopListenerAndJoin();

    return 0;
}
