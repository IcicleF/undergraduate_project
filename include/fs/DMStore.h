#ifndef LocoFS_DMStore_H
#define LocoFS_DMStore_H

#include <pthread.h>
#include <ctime>
#include <string>

#include <boost/smart_ptr.hpp>

#include "EntryList.h"
#include "KVStore.h"
#include "inode.hpp"

#define ON_USE 0
#define DELETED 1
#define RENAMED 2
//#define DM_LEVEL_SIZE 14

namespace boost
{
    namespace serialization
    {
        template <class Archive>
        void serialize(Archive &ar, DirectoryInode &d, const unsigned int version)
        {
            ar & d.ctime;
            ar & d.gid;
            ar & d.uid;
            ar & d.mode;
            ar & d.status;
            ar & d.old_name;
            ar & d.uuid;
        }
    }  // namespace serialization
}  // namespace boost

class DMStore
{
    std::shared_ptr<KVStore> dm_store;
    std::shared_ptr<KVStore> uuid_store;
    std::shared_ptr<EntryList> ec_store;
    uint64_t uuid_counter;
    pthread_mutex_t *uuid_mutex;
    int32_t getValue(const std::string &uuid, DirectoryInode &di);
    int32_t setValue(const std::string &uuid, const DirectoryInode &di);

public:
    DMStore() : dm_store(new KVStore()), uuid_store(new KVStore()),
                ec_store(new EntryList()), uuid_counter(0)
    {
        dm_store->initDB("dm_store");
        uuid_store->initDB("uuid_store");
        uuid_mutex = new pthread_mutex_t;
        pthread_mutex_init(uuid_mutex, NULL);
    }
    ~DMStore()
    {
        if (uuid_mutex != NULL)
            pthread_mutex_destroy(uuid_mutex);
    }

    int32_t mkdir(const std::string &path, const DirectoryInode &di);
    int32_t access(const std::string &path, const DirectoryInode &di);
    int32_t chown(const std::string &path, const DirectoryInode &di);
    int32_t chmod(const std::string &path, const DirectoryInode &di);
    int32_t rmdir(const std::string &path, const DirectoryInode &di);
    int32_t rename(const std::string &old_path, const std::string &new_path);
    void getAttr(DirectoryInode &_return, const std::string &path, const DirectoryInode &di);
    void readdir(std::string &_return, const std::string &path);
    int32_t opendir(const std::string &path, const DirectoryInode &di);
    int32_t setattr(const std::string &path, const DirectoryInode &di);
    int32_t get_uuid(const std::string &path, std::string &uuid);
    int32_t get_2_uuid(const std::string &path, std::string &puuid, std::string &uuid);
};

#endif  // LocoFS_DMStore_H
