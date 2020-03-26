#include <debug.hpp>
#include <fs/kvstore.hpp>

using std::string;
namespace rdb = rocksdb;

KVStore::~KVStore()
{
    if (db)
        delete db;
}

/** Initialize a RocksDB KVSTORE instance. */
void KVStore::init()
{
    bool expected = false;
    if (!pmemOccupied.compare_exchange_weak(expected, true)) {
        expectTrue(expected);
        d_err("pmem device has been occupied!");
        exit(-1);
    }

    const string &pmemDev = cmdConf->pmemDeviceName;

    rdb::Options options;
    options.create_if_missing = true;

    rdb::Status status = rdb::DB::Open(options, pmemDev, &db);
    if (!status.ok()) {
        d_err("cannot open RocksDB");
        exit(-1);
    }
}

/** Put a key/value pair to the database. */ 
bool KVStore::set(const string &key, const string &value)
{
    rdb::Status status = db->Put(writeOptions, key, value);
    return status.ok();
}

/** Get a value from the database. */
bool KVStore::get(const string &key, string &value)
{
    rdb::Status status = db->Get(readOptions, key, &value);
    return status.ok();
}

/** Remove a key/value pair from the database. */
bool KVStore::remove(const string &key)
{
    rdb::Status status = db->Delete(writeOptions, key);
    return status.ok();
}
