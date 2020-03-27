#ifndef LocoFS_FMStore_H
#define LocoFS_FMStore_H

#include <ctime>
#include <string>

#include "EntryList.h"
#include "KVStore.h"
#include "inode.hpp"
/**
 * This is for "Store Entry as obj" modification
 */

namespace boost
{
    namespace serialization
    {
        template <class Archive>
        void serialize(Archive &ar, FileAccessInode &d, const unsigned int version)
        {
            ar & d.ctime;
            ar & d.gid;
            ar & d.uid;
            ar & d.mode;
        }
        template <class Archive>
        void serialize(Archive &ar, FileContentInode &d, const unsigned int version)
        {
            ar & d.atime;
            ar & d.block_size;
            ar & d.mtime;
            ar & d.size;
            ar & d.origin_name;
            ar & d.suuid;
            ar & d.sid;
        }
    }  // namespace serialization
}  // namespace boost

class FMStore
{
    std::shared_ptr<KVStore> file_access_store;
    std::shared_ptr<KVStore> file_content_store;
    /**
     * need vram cache data?
     */
    std::shared_ptr<EntryList> ec_store;
    /**
     * This is for "Store Entry as obj" modification
     */
    //std::vector<DataServerRpc> data_trans;
    uint64_t suuid_counter;
    uint64_t sid_num;
    int32_t getValue(const std::string &Key, FileAccessInode &fa);
    int32_t setValue(const std::string &Key, const FileAccessInode &fa);
    int32_t getValue(const std::string &Key, FileContentInode &fc);
    int32_t setValue(const std::string &Key, const FileContentInode &fc);
    int32_t getValue(const std::string &Key, FileInode &fc);
    int32_t setValue(const std::string &Key, const FileInode &fc);

public:
    FMStore(int64_t sid) : file_access_store(new KVStore()), file_content_store(new KVStore()), ec_store(new EntryList()), suuid_counter(0)
    {
        sid_num = sid;
        file_access_store->initDB("file_access_store");
        file_content_store->initDB("file_content_store");
        /**
         * This is for "Store Entry as obj" modification
         */
        /**
         * To-do
         */
        //data_trans.push_back(*new DataServerRpc("127.0.0.1", 3334));
    }
    int32_t create(const std::string &Key, const FileAccessInode &fa);
    int32_t access(const std::string &Key, const FileAccessInode &fa);
    int32_t chown(const std::string &Key, const FileAccessInode &fa);
    int32_t chmod(const std::string &Key, const FileAccessInode &fa);
    int32_t remove(const std::string &Key, const FileAccessInode &fa);
    int32_t csize(const std::string &Key, const FileContentInode &fc);
    void getAttr(FileInode &_return, const std::string &Key, const FileInode &fi);
    void getContent(FileContentInode &_return, const std::string &Key, const FileContentInode &fi);
    void getAccess(FileAccessInode &_return, const std::string &Key, const FileAccessInode &fa);
    void readdir(std::string &_return, const int64_t uuid);
    int32_t utimens(const std::string &Key, const FileContentInode &fc);
    void open(FileInode &_return, const std::string &Key, const FileAccessInode &fa);
    int32_t rename(const std::string &old_path, const std::string &new_path);
};
#endif  // LocoFS_FMStore_H
