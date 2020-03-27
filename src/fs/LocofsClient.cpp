#include <fs/LocofsClient.h>
#include <config.hpp>
#include <network/message.hpp>

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

#define SERVER(A, B) A[location(B) % A.size()]

bool LocofsClient::mount(const std::string &conf)
{
    parseConfig();
    UCache.init(5000);
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
    struct loco_file_stat loco_st;
    if (_get_file_stat(path, loco_st) == false) {
        return false;
    }

    /**
     *  Write data begin
     */
    int64_t block_size = loco_st.block_size;
    int64_t lenth = len;
    int64_t offset = off;
    int64_t start = 0;
    while (lenth > 0) {
        int64_t block_num = offset / block_size;
        int64_t block_off = offset % block_size;
        int64_t block_len = block_size - block_off;
        block_len = lenth > block_len ? block_len : lenth;
        std::string Key_Obj;
        _get_object_key(path, block_num, Key_Obj);
        std::string b;
        b.append(buf + start, block_len);
        if (SERVER(data_trans, Key_Obj).write(Key_Obj, block_len, block_off, b) < 0) {
            //LOG(ERROR)<< path << " Write Data Block(Obj) fail";
            return false;
        }
        //LOG(INFO)<<path<<" Key_Obj="<<Key_Obj<<" block_num="<<block_num<<" block_off="<< block_off<<" block_len="<<block_len<<" block_size="<<block_size;
        start += block_len;
        lenth -= block_len;
        offset += block_len;
    }

    /**
     *  Get Key_FileInode
     */
    std::string Key_File;
    if (_get_file_key(path, Key_File) == false) {
        return false;
    }
    /**
     *  Update FileContentInode
     */
    FileContentInode fci;
    _set_ContentInode(loco_st, fci);
    fci.size = fci.size > (off + len) ? fci.size : (off + len);
    fci.mtime = time(NULL);
    fci.atime = time(NULL);
    if (SERVER(file_trans, Key_File).setContent(Key_File, fci) < 0) {
        //LOG(ERROR) << path << " update FileContentInode fail";
        return false;
    }
    //LOG(INFO) << path << " update FileContentInode success";
    /**
     *  remove FileStat cache, cause read only
     */
    // if (FCache.remove(Key_File) == false) {
    //     //LOG(ERROR) << path << " remove LocalCache fail";
    // }
    // //LOG(INFO) << path << " remove LocalCache success";
    //LOG(WARNING)<<" <<< File Write End >>> "<< path << " write sucess"<<" thread_id="<<pthread_self();
    return true;
}

int64_t LocofsClient::read(const std::string &path, std::string &buf, int64_t len, int64_t off)
{
    //LOG(WARNING)<< " <<< File Read Begin >>> " << path << " lenth="<<len<< " offset=" << off;
    /**
     *  Get file stat
     */
    struct loco_file_stat loco_st;
    if (_get_file_stat(path, loco_st) == false) {
        return false;
    }

    /**
     *  Read data begin
     */
    int64_t block_size = loco_st.block_size;
    int64_t size = loco_st.st.st_size;
    int64_t lenth = size < len ? size : len;
    int64_t offset = off;
    if (off + lenth > size) {
        //LOG(ERROR) << path << " read failed, read size is large than file size";
        return -1;
    }
    while (lenth > 0) {
        int64_t block_num = offset / block_size;
        int64_t block_off = offset % block_size;
        int64_t block_len = block_size - block_off;
        block_len = lenth > block_len ? block_len : lenth;
        std::string Key_Obj;
        _get_object_key(path, block_num, Key_Obj);
        std::string tmp;
        if (SERVER(data_trans, Key_Obj).read(Key_Obj, block_len, block_off, tmp) < 0) {
            //LOG(ERROR)<< path << " Read Data Block(Obj) fail";
            return false;
        }
        //LOG(INFO)<<path<<" Key_Obj="<<Key_Obj<<" block_num="<<block_num<<" block_off"<< block_off<<" block_len="<<block_len;
        buf.append(tmp.c_str(), block_len);
        lenth -= block_len;
        offset += block_len;
    }
    //LOG(INFO)<<" Read Content: "<<buf.substr(0,20)<< "...";

    /**
     *  Get Key_FileInode
     */
    std::string Key_File;
    if (_get_file_key(path, Key_File) == false) {
        return false;
    }
    /**
     *  Update FileContentInode
     */
    FileContentInode fci;
    _set_ContentInode(loco_st, fci);
    fci.atime = time(NULL);
    if (SERVER(file_trans, Key_File).setContent(Key_File, fci) == -1) {
        //LOG(ERROR) << path << " update metadata failed";
    }
    else {
        //LOG(INFO) << path << " metadata updated";
    }
    /**
     *  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
     *  should also update cache, if FileContentInode in cache
     */
    //LOG(WARNING)<<" <<< File Read End >>> "<< path << " Read sucess"<<" thread id"<<pthread_self();
    return buf.size();
}

bool LocofsClient::mkdir(const std::string &path, int32_t mode)
{
    std::string p;
    _check_path(path, p);

    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_MKDIR;
    strncpy(request.data.rpc.path, p.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(directory_trans, &request, &response);
    return (response.data.rpc.result == 0);
}

bool LocofsClient::opendir(const std::string &path, int32_t mode)
{
    std::string p;
    _check_path(path, p);
    
    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_OPENDIR;
    strncpy(request.data.rpc.path, p.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    request.data.rpc.mode = mode;
    ecal.getRPCInterface()->rpcCall(directory_trans, &request, &response);
    return (response.data.rpc.result == 0);
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

    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_ACCESS;
    strncpy(request.data.rpc.path, Key_File.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(SERVER(file_trans, Key_File), &request, &response);
    
    if (response.data.rpc.result == 0)
        return true;
    
    request.data.rpc.type = RPCMessage::RPC_CREATE;
    request.data.rpc.mode = 0777;
    ecal.getRPCInterface()->rpcCall(SERVER(file_trans, Key_File), &request, &response);

    return response.data.rpc.result == 0;
}

/**
 * remove dir only when it is empty
 */
bool LocofsClient::rmdir(const std::string &path)
{
    std::string p;
    _check_path(path, p);
    
    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_RMDIR;
    strncpy(request.data.rpc.path, p.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(directory_trans, &request, &response);

    if (response.data.rpc.result == 0) {
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

    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_REMOVE;
    strncpy(request.data.rpc.path, Key_File.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(SERVER(file_trans, Key_File), &request, &response);

    return response.data.rpc.result == 0;
}

/**
 * @param  buf  linux stat struct
 */
bool LocofsClient::stat(const std::string &path, struct stat &buf)
{
    std::string Key_File;
    if (_get_file_key(path, Key_File) == false)
        return false;

    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_STAT;
    strncpy(request.data.rpc.path, Key_File.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(SERVER(file_trans, Key_File), &request, &response);

    if (response.data.rpc.result < 0)
        return false;
    
    memcpy(&buf, &reinterpret_cast<loco_file_stat *>(response.data.rpc.raw)->st, sizeof(struct stat));
    buf.st_dev = location(Key_File) % file_trans.size();
    buf.st_ino = location(Key_File);
    return true;
}

/**
 * @param  buf  linux stat struct
 */
bool LocofsClient::statdir(const std::string &path, struct stat &buf)
{
    std::string p;
    _check_path(path, p);

    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_STAT;
    strncpy(request.data.rpc.path, p.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(directory_trans, &request, &response);

    if (response.data.rpc.result < 0)
        return false;
    
    memcpy(&buf, &reinterpret_cast<loco_dir_stat *>(response.data.rpc.raw)->st, sizeof(struct stat));
    buf.st_dev = 0;
    buf.st_mode = S_IFDIR | buf.st_mode;
    buf.st_ino = location(p);
    return true;
}

bool LocofsClient::readdir(const std::string &path, std::vector<std::string> &buf)
{
    std::string p;
    _check_path(path, p);
    buf.clear();

    /* Read subdir entries */
    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_READDIR;
    strncpy(request.data.rpc.path, p.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(directory_trans, &request, &response);

    std::string vbuf(reinterpret_cast<char *>(response.data.rpc.raw), response.data.rpc.result);
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
    request.data.rpc.raw64[0] = uuid;
    for (int i = 0; i < file_trans.size(); i++) {
        std::vector<std::string> temp;

        ecal.getRPCInterface()->rpcCall(file_trans[i], &request, &response);
        vbuf = std::string(reinterpret_cast<char *>(response.data.rpc.raw), response.data.rpc.result);
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
    
    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_CREATE;
    strncpy(request.data.rpc.path, Key_File.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    request.data.rpc.mode = mode;
    ecal.getRPCInterface()->rpcCall(SERVER(file_trans, Key_File), &request, &response);
    return (response.data.rpc.result == 0);
}


size_t LocofsClient::location(std::string path)
{
    std::hash<std::string> hash_str;
    return hash_str(path);
}

bool LocofsClient::parseConfig()
{
    for (int i = 0; i < clusterConf->getClusterSize(); ++i) {
        NodeConfig conf = (*clusterConf)[i];
        data_trans.push_back(conf.id);
        /* TODO: use config instead of hardcoded ID */
        if (conf.id == 0)
            directory_trans = conf.id;
        if (conf.id == 1)
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
    
    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_STAT;
    strncpy(request.data.rpc.path, path.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(directory_trans, &request, &response);

    if (response.data.rpc.result < 0)
        return false;
    
    uuid = reinterpret_cast<loco_dir_stat *>(response.data.rpc.raw)->uuid;
    UCache.set(path, uuid);
    return true;
}

bool LocofsClient::_get_file_key(const std::string &path, std::string &Key_File)
{
    boost::filesystem::path tmp(path);
    boost::filesystem::path parent = tmp.parent_path();
    boost::filesystem::path filename = tmp.filename();
    std::vector<std::string> vkey;

    uint64_t uuid;
    if (_get_uuid(parent.string(), uuid, true) == false)
        return false;

    vkey.push_back(std::to_string(uuid));
    vkey.push_back(filename.string());
    Key_File = boost::join(vkey, ":");
    return true;
}

bool LocofsClient::_get_file_stat(const std::string &path, loco_file_stat &loco_st)
{
    std::string Key_File;
    _get_file_key(path, Key_File);

    Message request, response;
    request.type = Message::MESG_RPC_CALL;
    request.data.rpc.type = RPCMessage::RPC_STAT;
    strncpy(request.data.rpc.path, Key_File.c_str(), MAX_PATH_LEN);
    request.data.rpc.path[MAX_PATH_LEN] = 0;
    ecal.getRPCInterface()->rpcCall(SERVER(file_trans, Key_File), &request, &response);

    memcpy(&loco_st, response.data.rpc.raw, sizeof(loco_file_stat));
    return (response.data.rpc.result == 0);
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
