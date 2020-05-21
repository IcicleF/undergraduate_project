#if !defined(INODE_HPP)
#define INODE_HPP

#include <cstdint>
#include <string>
#include <sys/stat.h>

struct ReadData
{
    std::string buf;
    int64_t size;
    int error;
};

struct FileAccessInode
{
    int32_t mode;
    int64_t ctime;
    int64_t uid;
    int64_t gid;
    int32_t error;
};

struct FileContentInode
{
    int64_t mtime;
    int64_t atime;
    int64_t size;
    int64_t block_size;
    std::string origin_name;
    int error;
    int64_t suuid;
    int64_t sid;
};

struct DirectoryInode
{
    int64_t ctime;
    int64_t mode;
    int64_t uid;
    int64_t gid;
    int64_t status;
    std::string old_name;
    int error;
    int64_t uuid;
};

struct FileInode
{
    FileContentInode fc;
    FileAccessInode fa;
    int error;
};


struct loco_dir_stat{
    struct stat st;
    int64_t uuid;
};

struct loco_file_stat{
    struct stat st;
    uint64_t suuid;
    uint64_t sid;
    uint64_t block_size;
};

#endif // INODE_HPP
