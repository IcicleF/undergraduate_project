#include <fs/KVStore.h>

bool KVStore::initDB(std::string db_name){
    if (db->open(db_name, kyotocabinet::CacheDB::OWRITER | kyotocabinet::CacheDB::OCREATE)==false){
        //LOG(FATAL) << "open error" << db->error().name();
        return false;
    }
    //LOG(INFO)<<"KV init success";
    return true;
}

bool KVStore::getValue(const std::string &path, std::string &value)
{
	bool temp;
    temp=db->get(path,&value);
    if(temp==false)
    {
    	//LOG(WARNING)<<path<<" get KV failed";
    } else {
    	//LOG(INFO)<<"get KV success: Key="<<path<<", Value="<<value;
    }
    return temp;
}

/**
 * 	if ( query failed ) {
 * 		insert;
 * 	} else {
 * 		update;
 * 	}
 */
bool KVStore::setValue(const std::string &path, const std::string & value)
{
	bool temp;
    temp=db->set(path,value);
    if(temp==false)
    {
       //LOG(ERROR)<<path<<" set KV failed";
    }
    return temp;
}

bool KVStore::remove(const std::string &path)
{
	return db->remove(path);
}
