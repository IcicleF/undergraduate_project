#ifndef LocoFS_KVStore_H
#define LocoFS_KVStore_H

#include <string>
#include <kccachedb.h>

class KVStore{
private:
	kyotocabinet::CacheDB *db;
public:
    KVStore():db(new kyotocabinet::CacheDB()){};
    ~KVStore(){
        db->close();
    };
    bool initDB(std::string db_name);
    bool getValue(const std::string &path, std::string &value);
    bool setValue(const std::string &path, const std::string & value);
    bool remove(const std::string &path);
};
#endif // LocoFS_KVStore_H
