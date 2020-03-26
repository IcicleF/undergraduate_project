/******************************************************************
 * This file is part of Galois (modified from LocoFS).            *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(DMS_HPP)
#define DMS_HPP

#include "kvstore.hpp"
#include "entrylist.hpp"
#include "inode.hpp"

#define ON_USE 0
#define DELETED 1
#define RENAMED 2

namespace boost
{
    namespace serialization
    {
        template <class Archive>
        void serialize(Archive &ar, DirectoryInode &d, const unsigned int version)
        {
            ar & d.gid;
            ar & d.uid;
            ar & d.mode;
            ar & d.status;
            ar & d.name;
            ar & d.uuid;
        }
    }
}

/**
 * Directory metadata storage.
 */
class DMStore
{
public:
    explicit DMStore() : dms(new KVStore()), ecs(new EntryList()), uuidCounter(0)
    {
        dms->init();
    }
    ~DMStore() = default;

    int mkdir(const std::string &path, const DirectoryInode &di);
    int access(const std::string &path, const DirectoryInode &di);
    int chown(const std::string &path, const DirectoryInode &di);
    int chmod(const std::string &path, const DirectoryInode &di);
    int rmdir(const std::string &path, const DirectoryInode &di);
    int rename(const std::string &old_path, const std::string &new_path);
    void getattr(DirectoryInode &_return, const std::string &path, const DirectoryInode &di);
    void readdir(std::string &_return, const std::string &path);
    int opendir(const std::string &path, const DirectoryInode &di);
    int setattr(const std::string &path, const DirectoryInode &di);
    int getUuid(const std::string &path, std::string &uuid);
    int get2Uuid(const std::string &path, std::string &puuid, std::string &uuid);

private:
    std::shared_ptr<KVStore> dms;
    std::shared_ptr<EntryList> ecs;
    std::atomic<uint64_t> uuidCounter;
    
    int getInode(const std::string &uuid, DirectoryInode &di);
    int setInode(const std::string &uuid, const DirectoryInode &di);
};

#endif  // DMS_HPP
