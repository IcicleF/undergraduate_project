/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(KVSTORE_HPP)
#define KVSTORE_HPP

#include <rocksdb/db.h>

#include "../config.hpp"

/**
 * Simple wrapper of RocksDB (in-NVM).
 * The implementation makes sure that only one KVSTORE instance exists on every node.
 */
class KVStore
{
public:
    explicit KVStore() = default;
    ~KVStore();
    KVStore(const KVStore &) = delete;
    KVStore &operator=(const KVStore &) = delete;

    void init();
    bool set(const std::string &key, const std::string &value);
    bool get(const std::string &key, std::string &value);
    bool remove(const std::string &key);

private:
    rocksdb::DB *db;
    rocksdb::ReadOptions readOptions;
    rocksdb::WriteOptions writeOptions;
};

#endif // KVSTORE_HPP
