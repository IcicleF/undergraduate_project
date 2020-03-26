/******************************************************************
 * This file is part of Galois (modified from LocoFS).            *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(INODE_HPP)
#define INODE_HPP

#include <cstdint>
#include <string>

struct DirectoryInode
{
    int64_t mode;
    int64_t uid;
    int64_t gid;
    int64_t status;
    int64_t uuid;
    int error;
    std::string name;
};

struct FileAccessInode
{
    int64_t ctime;
    int64_t uid;
    int64_t gid;
    uint32_t mode;
    int error;
};

struct FileContentInode
{
    int64_t mtime;
    int64_t atime;
    int64_t size;
    int64_t blockSize;
    int64_t suuid;
    int64_t sid;
    int error;
    std::string name;
};

struct FileInode
{
    FileContentInode fc;
    FileAccessInode fa;
    int error;
};

#endif // INODE_HPP
