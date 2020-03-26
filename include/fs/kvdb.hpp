#if !defined(KVDB_HPP)
#define KVDB_HPP

#include <rocksdb/db.h>

#include "../config.hpp"

/**
 * Simple wrapper of RocksDB (in-NVM).
 * The implementation makes sure that only one KVDB instance exists on every node.
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

#endif // KVDB_HPP
