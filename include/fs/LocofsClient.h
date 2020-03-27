#ifndef LocoFS_LocofsClient_H
#define LocoFS_LocofsClient_H

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <functional>
#include <string>
#include <vector>

#include "lru_cache.h"
#include "inode.hpp"
#include "../ecal.hpp"

class LocofsClient
{
public:
    LocofsClient() = default;
    ~LocofsClient() = default;

    bool mount(const std::string &conf);
    void stop()
    {
        ecal.getRPCInterface()->stopListenerAndJoin();
    }

    bool write(const std::string &path, const char *buf, int64_t len, int64_t off);
    int64_t read(const std::string &path, char *buf, int64_t len, int64_t off);

    bool mkdir(const std::string &path, int32_t mode);
    bool open(const std::string &path, int32_t flags);
    bool create(const std::string &path, int32_t mode);
    bool close(const std::string &path);

    bool unlink(const std::string &path);
    bool rmdir(const std::string &path);
    bool opendir(const std::string &path, int32_t mode);
    bool readdir(const std::string &path, std::vector<std::string> &buf);

    bool stat(const std::string &path, struct stat &buf);
    bool statdir(const std::string &path, struct stat &buf);
    
private:
    size_t location(std::string path);
    bool parseConfig();
    bool _get_uuid(const std::string &path, uint64_t &uuid, const bool IS_SetCache);
    bool _get_file_key(const std ::string &path, std ::string &Key_File);
    bool _get_file_stat(const std::string &path, struct loco_file_stat &loco_st);
    bool _get_object_key(const std::string &path, int64_t oid, std::string &Key_Obj);
    bool _set_ContentInode(const struct loco_file_stat &loco_st, FileContentInode &fci);
    bool _check_path(const std::string &path, std::string &p);

    int directory_trans;
    std::vector<int> file_trans;
    std::vector<int> data_trans;
    // lru_cache<std::string, struct loco_dir_stat> DCache;
    // lru_cache<std::string, struct loco_file_stat> FCache;
    // cache dir uuid, for file access
    lru_cache<std::string, uint64_t> UCache;

    ECAL ecal;
};
#endif  // LocoFS_LocofsClient_H
