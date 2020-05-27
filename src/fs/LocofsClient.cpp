#include <fs/LocofsClient.h>
#include <config.hpp>
#include <network/msg.hpp>

#include <fcntl.h>
#include <pthread.h>
#include <ctime>
#include <map>
#include <numeric>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>


#define SERVER(A, B) A[0]


long boost_cpu_time = 0;
long meta_rpc_time = 0;
long data_rdma_time_r = 0, data_rdma_time_w = 0;
long meta_upd_time_r = 0, meta_upd_time_w = 0;

extern int readCount;

inline uint64_t hashObj(struct loco_file_stat st, int blkid)
{
    uint64_t ret = 0;
    ret ^= st.sid * 2654435761;
    ret = (ret << 32) ^ ret;
    ret ^= st.suuid * 2654435761;
    ret = (ret << 27) ^ ret;
    ret ^= blkid * 2654435761;
    ret = (ret << 37) ^ ret;
    return ret;
}


bool LocofsClient::mount(const std::string &conf)
{
    parseConfig();
    UCache.init(5000);
    ecal.regNetif(&netif);
    return true;
}

bool LocofsClient::write(const std::string &path, const char *buf, int64_t len, int64_t off)
{
    //LOG(WARNING)<< " <<< File Write Begin >>> "<< path << " lenth="<<len<< " offset=" << off;
    /**
     *  Get FileStat
     *      both file access and content inode
     *          block_size for data write
     *          others for FileContentInode update
     */

    using std::chrono::steady_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    //auto stt = steady_clock::now();
    //auto edt = steady_clock::now();

    //printf("entry "); fflush(stdout);

    //auto stt = steady_clock::now();
    struct loco_file_stat loco_st;
    if (_get_file_stat(path, loco_st) == false) {
        return false;
    }
    //auto edt = steady_clock::now();
    //meta_rpc_time += duration_cast<microseconds>(edt - stt).count();

    //d_info("_get_file_stat succ");
    //printf("_g "); fflush(stdout);

    /**
     *  Write data begin
     */
    int64_t block_size = loco_st.block_size;
    int64_t offset = off, length = len;
    int64_t start = 0;

    ECAL::Page page;
    //stt = steady_clock::now();
    while (len > 0) {
        int64_t block_num = offset / block_size;            // # of data block
        int64_t block_off = offset % block_size;            // offset in the current block
        int64_t block_len = block_size - block_off;         
        block_len = len > block_len ? block_len : len;      // length in the current block

        uint64_t blkno = hashObj(loco_st, block_num) % ecal.getClusterCapacity();
        if (block_off || block_off + block_len < Block4K::size) {
            /* Needs a read; TODO: remove read */
            ecal.readBlock(blkno, page);
            memcpy(page.page.data + block_off, buf + start, block_len);
            ecal.writeBlock(page);
        }
        else {
            /* Full page */
            memcpy(page.page.data, buf + start, block_len);
            ecal.writeBlock(page);
        }
        
        start += block_len;
        len -= block_len;
        offset += block_len;
    }
    //edt = steady_clock::now();
    //data_rdma_time_w += duration_cast<microseconds>(edt - stt).count();

    //d_info("EC & RDMA succ");

    /**
     *  Get Key_FileInode
     */
    std::string Key_File;
    //stt = steady_clock::now();
    if (_get_file_key(path, Key_File) == false)
        return false;
    //edt = steady_clock::now();
    //meta_rpc_time += duration_cast<microseconds>(edt - stt).count();
    
    //d_info("_get_file_key succ");

    /**
     *  Update FileContentInode
     */
    FileContentInode fci;
    _set_ContentInode(loco_st, fci);
    fci.size = fci.size > (off + length) ? fci.size : (off + length);
    
    //stt = steady_clock::now();
    ValueWithPathRequest request;
    {
        request.value = fci.size;
        strncpy(request.path, Key_File.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    PureValueResponse response;
    netif.rpcCall(SERVER(file_trans, Key_File), ErpcType::ERPC_CSIZE, request, response);
    //edt = steady_clock::now();
    //meta_upd_time_w += duration_cast<microseconds>(edt - stt).count();

    //d_info("csize rpc succ");

    return (response.value == 0);
}

int64_t LocofsClient::read(const std::string &path, char *buf, int64_t len, int64_t off)
{
    //LOG(WARNING)<< " <<< File Read Begin >>> " << path << " lenth="<<len<< " offset=" << off;
    /**
     *  Get file stat
     */
    using std::chrono::steady_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    //auto stt = steady_clock::now();
    //auto edt = steady_clock::now();

    //auto stt = steady_clock::now();
    struct loco_file_stat loco_st;
    if (_get_file_stat(path, loco_st) == false)
        return false;
    //auto edt = steady_clock::now();
    //meta_rpc_time += duration_cast<microseconds>(edt - stt).count();

    /**
     *  Read data begin
     */
    int64_t block_size = loco_st.block_size;
    int64_t size = loco_st.st.st_size;
    len = size < len ? size : len;
    int64_t offset = off;
    int64_t start = 0;
    if (off + len > size) {
        //LOG(ERROR) << path << " read failed, read size is large than file size";
        return -1;
    }

    ECAL::Page page;
    //stt = steady_clock::now();
    while (len > 0) {
        int64_t block_num = offset / block_size;            // # of data block
        int64_t block_off = offset % block_size;            // offset in the current block
        int64_t block_len = block_size - block_off;
        block_len = len > block_len ? block_len : len;      // length in the current block

        uint64_t blkno = hashObj(loco_st, block_num) % ecal.getClusterCapacity();
        ecal.readBlock(blkno, page);
        memcpy(buf + start, page.page.data + block_off,  block_len);
        
        start += block_len;
        len -= block_len;
        offset += block_len;
    }
    //edt = steady_clock::now();
    //data_rdma_time_r += duration_cast<microseconds>(edt - stt).count();
    
    return start;
}

bool LocofsClient::mkdir(const std::string &path, int32_t mode)
{
    std::string p;
    _check_path(path, p);

    //d_info("mkdir: %s", p.c_str());

    ValueWithPathRequest request;
    {
        strncpy(request.path, p.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    PureValueResponse response;
    netif.rpcCall(directory_trans, ErpcType::ERPC_MKDIR, request, response);
    return (response.value == 0);
}

bool LocofsClient::opendir(const std::string &path, int32_t mode)
{
    return false;
}

/**
 * if file does not exist, create one: FileInode, EntryList
 */
bool LocofsClient::open(const std::string &path, int32_t flags)
{
    /**
     *  base on file dir path,
     *      get the key of it's fileinode
     *  IMP:
     *      return false, if dir not exists
     */
    std::string Key_File;
    if (_get_file_key(path, Key_File) == false) {
        return false;
    }

    ValueWithPathRequest request;
    {
        strncpy(request.path, Key_File.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    PureValueResponse response;
    netif.rpcCall(SERVER(file_trans, Key_File), ErpcType::ERPC_ACCESS, request, response);
    
    if (response.value == 0)
        return true;
    
    request.value = 0777;
    netif.rpcCall(SERVER(file_trans, Key_File), ErpcType::ERPC_CREATE, request, response);
    return response.value == 0;
}

/**
 * remove dir only when it is empty
 */
bool LocofsClient::rmdir(const std::string &path)
{
    std::string p;
    _check_path(path, p);
    
    ValueWithPathRequest request;
    {
        strncpy(request.path, p.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    PureValueResponse response;
    netif.rpcCall(directory_trans, ErpcType::ERPC_RMDIR, request, response);

    if (response.value == 0) {
        UCache.remove(p);
        return true;
    }
    return false;
}

bool LocofsClient::unlink(const std::string &path)
{
    std::string Key_File;
    if (_get_file_key(path, Key_File) == false)
        return false;

    ValueWithPathRequest request;
    {
        strncpy(request.path, Key_File.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    PureValueResponse response;
    netif.rpcCall(SERVER(file_trans, Key_File), ErpcType::ERPC_REMOVE, request, response);
    return (response.value == 0);
}

/**
 * @param  buf  linux stat struct
 */
bool LocofsClient::stat(const std::string &path, struct stat &buf)
{
    std::string Key_File;
    if (_get_file_key(path, Key_File) == false)
        return false;

    ValueWithPathRequest request;
    {
        strncpy(request.path, Key_File.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    StatResponse response;
    netif.rpcCall(SERVER(file_trans, Key_File), ErpcType::ERPC_FILESTAT, request, response);

    if (response.result < 0)
        return false;
    
    memcpy(&buf, &response.fileStat.st, sizeof(struct stat));
    return true;
}

/**
 * @param  buf  linux stat struct
 */
bool LocofsClient::statdir(const std::string &path, struct stat &buf)
{
    std::string p;
    _check_path(path, p);

    ValueWithPathRequest request;
    {
        strncpy(request.path, p.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    StatResponse response;
    netif.rpcCall(directory_trans, ErpcType::ERPC_DIRSTAT, request, response);

    if (response.result < 0)
        return false;
    
    memcpy(&buf, &response.dirStat.st, sizeof(struct stat));
    buf.st_dev = 0;
    buf.st_mode = S_IFDIR | buf.st_mode;
    buf.st_ino = 0;
    return true;
}

bool LocofsClient::readdir(const std::string &path, std::vector<std::string> &buf)
{
    std::string p;
    _check_path(path, p);
    buf.clear();

    /* Read subdir entries */
    ValueWithPathRequest request;
    {
        strncpy(request.path, p.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    RawResponse response;
    netif.rpcCall(directory_trans, ErpcType::ERPC_READDIR, request, response);

    std::string vbuf(response.raw, response.len);
    boost::split(buf, vbuf, boost::is_any_of("\t"), boost::token_compress_on);

    /**
     *  get dir uuid, set Cache
     */
    uint64_t uuid;
    if (_get_uuid(p, uuid, true) == false)
        return false;
    
    /**
     *  iterate all FMServers
     *      to get sub_file entries
     */
    PureValueRequest req2;
    req2.value = uuid;
    for (int i = 0; i < file_trans.size(); i++) {
        std::vector<std::string> temp;

        netif.rpcCall(file_trans[i], ErpcType::ERPC_READDIR, req2, response);
        vbuf = std::string(response.raw, response.len);
        boost::split(temp, vbuf, boost::is_any_of("\t"), boost::token_compress_on);

        if (!temp.empty())
            buf.insert(buf.end(), temp.begin(), temp.end());
    }
    return true;
}

bool LocofsClient::create(const std::string &path, int32_t mode)
{
    std::string p;
    std::string Key_File;
    _check_path(path, p);

    if (_get_file_key(p, Key_File) == false)
        return false;
    
    ValueWithPathRequest request;
    {
        strncpy(request.path, Key_File.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    PureValueResponse response;
    netif.rpcCall(SERVER(file_trans, Key_File), ErpcType::ERPC_CREATE, request, response);
    return (response.value == 0);
}


size_t LocofsClient::location(std::string path)
{
    return 0;
}

bool LocofsClient::parseConfig()
{
    for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
        NodeConfig conf = (*clusterConf)[i];
        data_trans.push_back(conf.id);
        
        if (conf.type == NODE_DMS)
            directory_trans = conf.id;
        if (conf.type == NODE_FMS)
            file_trans.push_back(conf.id);
    }

    // DMS:   #0
    // FMS:   [#1]
    // Data:  [#0, #1, #2]
    return true;
}

/**
 * used by readdir & _get_file_key
 *     cuz used by _get_file_key which is called by many functions, we should check IS_SetCache.
 *     now only readdir will SetCache=1.
 * UPADTE:
 *     now _get_file_key(open/read/write/create/stat/unlink) alse SetCache=1.
 */
bool LocofsClient::_get_uuid(const std::string &path, uint64_t &uuid, const bool IS_SetCache)
{
    if (UCache.get(path, uuid))
        return true;

    //d_info("_get_uuid: %s", path.c_str());
    
    ValueWithPathRequest request;
    {
        strncpy(request.path, path.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    StatResponse response;
    netif.rpcCall(directory_trans, ErpcType::ERPC_DIRSTAT, request, response);

    if (response.result < 0)
        return false;
    
    uuid = response.dirStat.uuid;
    UCache.set(path, uuid);
    return true;
}

bool LocofsClient::_get_file_key(const std::string &path, std::string &Key_File)
{
    //auto stt = std::chrono::steady_clock::now();
    boost::filesystem::path tmp(path);
    boost::filesystem::path parent = tmp.parent_path();
    boost::filesystem::path filename = tmp.filename();
    //auto edt = std::chrono::steady_clock::now();
    //boost_cpu_time += std::chrono::duration_cast<std::chrono::microseconds>(edt - stt).count();

    std::vector<std::string> vkey;

    //stt = std::chrono::steady_clock::now();
    uint64_t uuid;
    if (_get_uuid(parent.string(), uuid, true) == false)
        return false;
    //edt = std::chrono::steady_clock::now();
    //meta_rpc_time += std::chrono::duration_cast<std::chrono::microseconds>(edt - stt).count();

    //stt = std::chrono::steady_clock::now();
    vkey.push_back(std::to_string(uuid));
    vkey.push_back(filename.string());
    Key_File = boost::join(vkey, ":");
    //edt = std::chrono::steady_clock::now();
    //boost_cpu_time += std::chrono::duration_cast<std::chrono::microseconds>(edt - stt).count();

    return true;
}

bool LocofsClient::_get_file_stat(const std::string &path, loco_file_stat &loco_st)
{
    std::string Key_File;
    _get_file_key(path, Key_File);

    //auto stt = std::chrono::steady_clock::now();
    ValueWithPathRequest request;
    {
        strncpy(request.path, Key_File.c_str(), MAX_PATH_LEN);
        request.path[MAX_PATH_LEN] = 0;
    }
    StatResponse response;
    netif.rpcCall(SERVER(file_trans, Key_File), ErpcType::ERPC_FILESTAT, request, response);
    //auto edt = std::chrono::steady_clock::now();
    //meta_rpc_time += std::chrono::duration_cast<std::chrono::microseconds>(edt - stt).count();

    memcpy(&loco_st, &response.fileStat, sizeof(loco_file_stat));
    return (response.result == 0);
}

/**
 * base on file_path, obj_id
 *     return Key_Obj
 *     used by WRITE & READ
 */
bool LocofsClient::_get_object_key(const std::string &path, int64_t obj_id, std::string &Key_Obj)
{
    std::vector<std::string> vbuf;
    loco_file_stat loco_st;
    if (_get_file_stat(path, loco_st) == false)
        return false;
    
    vbuf.push_back(std::to_string(loco_st.sid));
    vbuf.push_back(std::to_string(loco_st.suuid));
    vbuf.push_back(std::to_string(obj_id));
    Key_Obj = boost::join(vbuf, ":");
    return true;
}

bool LocofsClient::_set_ContentInode(const struct loco_file_stat &loco_st, FileContentInode &fci)
{
    fci.mtime = loco_st.st.st_mtime;
    fci.atime = loco_st.st.st_atime;
    fci.size = loco_st.st.st_size;
    fci.block_size = loco_st.block_size;
    fci.suuid = loco_st.suuid;
    fci.sid = loco_st.sid;
    return true;
}

/**
 *  if not root dir
 *      delete last '/' from path
 */
bool LocofsClient::_check_path(const std::string &path, std::string &p)
{
    p = path;
    if (p.length() != 1 && p[p.length() - 1] == '/')
        p.erase(p.end() - 1);
    return true;
}


/** RPC roundtrip time in us */
uint64_t LocofsClient::testRoundTrip(int peerId)
{
    auto stt = std::chrono::steady_clock::now();

    PureValueRequest request;
    PureValueResponse response;
    netif.rpcCall(peerId, ErpcType::ERPC_TEST, request, response);

    auto edt = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(edt - stt).count();
}


/* Main function part */
#include <chrono>

DEFINE_MAIN_INFO();

void thptWorker(LocofsClient *cli, bool wl, int n = 100000)
{
    using namespace std::chrono;

    std::string path = "/test/0001";
    ECAL::Page page(0);
    memset(page.page.data, 'a', 4096);

    ECAL *ecal = cli->getECAL();
    if (wl)
        for (int i = 0; i < n; ++i) {
            auto stt = steady_clock::now();
            while (duration_cast<microseconds>(steady_clock::now() - stt).count() < 10);
            ecal->writeBlock(page);
        }
    else
        for (int i = 0; i < n; ++i) {
            auto stt = steady_clock::now();
            while (duration_cast<microseconds>(steady_clock::now() - stt).count() < 5);
            ecal->readBlock(1, page);
        }
}

int main(int argc, char **argv)
{
    using namespace std;
    using namespace std::chrono;

    COLLECT_MAIN_INFO();

    cmdConf = new CmdLineConfig();
    LocofsClient loco;

    expectTrue(loco.mount(""));

    printf("LocoFS Client mounted.\n");
    fflush(stdout);
    
    string filename = "/test/0001";
    expectTrue(loco.mkdir("/test", 0644));
    expectTrue(loco.create(filename, 0644));
    expectTrue(loco.open(filename, O_RDWR | O_CREAT));

    const int N = cmdConf->_N;

    srand(time(0));
    const int M = cmdConf->_Size;
    char buf[M];
    for (int i = 0; i < M; ++i)
        buf[i] = 'p';
/*
    d_info("start r/w...");

    auto start = steady_clock::now();
    for (int i = 0; i < N; ++i) {
        expectTrue(loco.write(filename, buf, M, i * M));
    }
    auto end = steady_clock::now();
    auto timespan = duration_cast<microseconds>(end - start).count();

    printf("\n");
    printf("Write %dKB: %.2lf us\n", M / 1024, (double)timespan / N);
    //printf("Breakdown:\n");
    //printf("- Boost CPU computation: %.2lf us\n", (double)boost_cpu_time / N);
    //printf("- Metadata fetch RPC: %.2lf us\n", (double)meta_rpc_time / N);
    //printf("- Data RDMA: %.2lf us\n", (double)data_rdma_time_w / N);
    //printf("\n");

    loco.testRoundTrip(0);
   
    boost_cpu_time = 0;
    meta_rpc_time = 0;

    start = steady_clock::now();
    for (int i = 0; i < N; ++i) {
        loco.read(filename, buf, M, i * M);
        if (errno) {
            printf("failed at %d\n", i);
            for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
                auto peerNode = (*clusterConf)[i];
                if (peerNode.id != myNodeConf->id)
                    continue;
                loco.getECAL()->getRDMASocket()->verboseQP(peerNode.id);
            }
            break;
        }
    }
    //printf("\n");
    end = steady_clock::now();
    timespan = duration_cast<microseconds>(end - start).count();

    printf("Read %dKB: %.2lf us\n\n", M / 1024, (double)timespan / N);
    //printf("Breakdown:\n");
    //printf("- Boost CPU computation: %.2lf us\n", (double)boost_cpu_time / N);
    //printf("- Metadata fetch RPC: %.2lf us\n", (double)meta_rpc_time / N);
    //printf("- Data RDMA: %.2lf us\n", (double)data_rdma_time_r / N);
    //printf("- Metadata update RPC: %.2lf us\n\n", (double)meta_upd_time_r / N);

    const int thnum = cmdConf->_Thread;
    if (thnum) {
        std::thread ths[16];

        start = steady_clock::now();
        for (int i = 1; i <= thnum; ++i)
            ths[i] = std::thread(thptWorker, &loco, true, 100000);
        for (int i = 1; i <= thnum; ++i)
            ths[i].join();
        end = steady_clock::now();
        timespan = duration_cast<milliseconds>(end - start).count();

        double thpt = 1000 * 1.0 * thnum / timespan * 100000;
        printf("%d thread(s): %.1lf\n\n", thnum, thpt);
    }
*/
    // Test first-k read
    auto stt = steady_clock::now();
    decltype(stt) Begin, End;
    std::vector<int> latw, latr;
    size_t Cnt = 0;
    int Trigger = 0;
    ibv_wc wc[2];
    
    while (true) {
        Begin = steady_clock::now();
        int dur = duration_cast<seconds>(Begin - stt).count();

        if (dur >= 20)
            break;

        ++Trigger;
        if (Trigger == 10)
            Trigger = 0;
        
        Begin = steady_clock::now();
        loco.read(filename, buf, 4096, 0);
        End = steady_clock::now();
        loco.getECAL()->getRDMASocket()->pollSendCompletion(wc);
        if (!Trigger)
            latr.push_back(duration_cast<microseconds>(End - Begin).count());
    }

    FILE *fout = fopen("log.txt", "w");
    for (int i = 0; i < latr.size(); ++i)
        fprintf(fout, "%d ", latr[i]);
    fprintf(fout, "\n");
    fclose(fout);
    loco.stop();

    return 0;
}
