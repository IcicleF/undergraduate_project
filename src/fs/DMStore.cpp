#include <fs/DMStore.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

int32_t DMStore::getValue(const std::string & uuid, DirectoryInode & di){
    std::string value;
    if(dm_store->getValue(uuid,value)==false){
        //LOG(WARNING)<<uuid<<"get DirectoryInode failed";
        return -1;
    }else{
        //LOG(INFO)<<uuid<<" get DirectoryInode success:"<<" size="<<value.size()<<" DirectoryInode="<<value;
        std::istringstream is(value);
        boost::archive::text_iarchive ia(is);
        ia>>di;
    }
    return 0;
}
int32_t DMStore::setValue(const std::string & uuid, const DirectoryInode & di){
    std::ostringstream os;
    boost::archive::text_oarchive oa(os);
    oa<<di;
    std::string value = os.str();
    if(dm_store->setValue(uuid,value)==false){
        //LOG(WARNING)<<uuid<<"set DirectoryInode failed:"<<" size="<<value.size()<<" DirectoryInode="<<value;
        return -1;
    }
    //LOG(INFO)<<uuid<<" set DirectoryInode success:"<<" size="<<value.size()<<" DirectoryInode="<<value;
    return 0;
}
int32_t DMStore::mkdir(const std::string & path, const DirectoryInode & di){
    uint64_t this_uuid;
    /**
     * Need atomic
     */
    pthread_mutex_lock(uuid_mutex);
    this_uuid = ++uuid_counter;
    pthread_mutex_unlock(uuid_mutex);

    /**
     * get parent_dir's uuid, dir's name
     */
    boost::filesystem::path tmp(path);
    boost::filesystem::path parent = tmp.parent_path();
    boost::filesystem::path filename = tmp.filename();
    std::string puuid;
    std::string key_uuid;

    if (path == "/") {
        key_uuid = "/";
    } else {
        if (get_uuid(parent.c_str(), puuid) < 0) {
            //LOG(WARNING)<<path<<" The directory not exits";
            return -1;
        }
        key_uuid.append(puuid);
        key_uuid.append(":");
        key_uuid.append(filename.c_str());
    }

    std::string tmp2;
    if(uuid_store->getValue(key_uuid, tmp2) == true){
      //LOG(WARNING)<<path<<" The directory already exits";
      return -1;
    }

    /**
     * update path->uuid
     */
    //LOG(INFO)<<"key_uuid="<<key_uuid<<" uuid="<<this_uuid;
    if (uuid_store->setValue(key_uuid, std::to_string(this_uuid)) == false) {
        return -1;
    } else {
        //LOG(INFO)<<path<<" kv stored dir path->uuid";
    }
    /**
     * update uuid->inode
     */
    DirectoryInode dii;
    dii.mode = di.mode;
    dii.gid = di.gid;
    dii.uid = di.uid;
    dii.ctime = time(NULL);
    dii.status = ON_USE;
    dii.old_name = path;
    dii.uuid = this_uuid;
    if(setValue(std::to_string(this_uuid), dii)<0){
        //LOG(ERROR)<<path<<" mkdir set uuid->inode failed";
        return -1;
    } else {
        //LOG(INFO)<<path<<" kv stored dir uuid->inode";
    }
    /**
     * update entry
     */
    if (path == "/") {
    } else {
        if (ec_store->insert_entry(puuid, filename.c_str())<0) {
        //LOG(WARNING)<<puuid<<"<"<<filename.c_str()<<"insert_entry failed";
        return -1;
        }
    }
    return 0;
}

int32_t DMStore::access(const std::string& path, const DirectoryInode& di){
    std::string uuid;
    if (get_uuid(path, uuid) < 0) {
        return -1;
    }
    DirectoryInode dii;
	if(getValue(uuid, dii) < 0){
		//LOG(WARNING)<<path<<" Dir access failed";
        return -1;
	}
    return 0;
}
int32_t DMStore::chown(const std::string& path, const DirectoryInode& di){
    std::string uuid;
    if (get_uuid(path, uuid) < 0) {
        return -1;
    }
    DirectoryInode dii;
    getValue(uuid, dii);
    dii.uid = di.uid;
    dii.gid = di.gid;
    dii.ctime = time(NULL);
    setValue(uuid, dii);
	return 0;
}
int32_t DMStore::chmod(const std::string& path, const DirectoryInode& di){
    std::string uuid;
    if (get_uuid(path, uuid) < 0) {
        return -1;
    }
    DirectoryInode dii;
    getValue(uuid, dii);
    dii.mode = di.mode;
    dii.ctime = time(NULL);
    setValue(uuid, di);
	return 0;
}
int32_t DMStore::rmdir(const std::string& path, const DirectoryInode& di) {
    if (path == "/") {
        return 1;
    }
    std::string uuid;
    std::string puuid;
    boost::filesystem::path tmp(path);
    boost::filesystem::path filename=tmp.filename();

    if (get_2_uuid(path, puuid, uuid) < 0) {
        //LOG(WARNING)<<path<<" The directory not exists";
        return -1;
    }
    /**
     * should check if dir empty!!!!!!!!!!!!
     */
    //to-do

    /**
     * remove uuid->inode
     */
    if(dm_store->remove(uuid)==false){
		//LOG(FATAL)<<path<<" The uuid->inode can not been removed";
        return -1;
    }
    //LOG(INFO)<<uuid<<" removed uuid->inode";
    /**
     * remove path->uuid
     */
    std::string key_uuid;
    key_uuid.append(puuid);
    key_uuid.append(":");
    key_uuid.append(filename.c_str());
    if(uuid_store->remove(key_uuid)==false) {
        //LOG(FATAL)<<key_uuid<<" The path->uuid can not been removed";
        return -1;
    }
    //LOG(INFO)<<key_uuid<<" removed key_uuid->uuid";
    /**
     * remove entry
     */
    // to-do BUG in concurrent
    if (ec_store->remove_entry(puuid, filename.c_str()) <0) {
        //LOG(WARNING)<<path<<" The entry can not been removed";
        return -1;
    }
    //LOG(INFO)<<puuid<<" removed entry";
	return 0;
}
int32_t DMStore::rename(const std::string& old_path, const std::string& new_path){
    /**
     * get old_key_uuid, new_key_uuid, uuid
     */
    std::string uuid;
    std::string puuid;
    if (get_2_uuid(old_path, puuid, uuid)<0) {
        //LOG(WARNING)<<old_path<<"no such dir";
        return -1;
    }

    boost::filesystem::path tmp_new(new_path);
    boost::filesystem::path filename_new = tmp_new.filename();
    std::string new_name = filename_new.c_str();

    boost::filesystem::path tmp_old(old_path);
    boost::filesystem::path filename_old = tmp_old.filename();
    std::string old_name = filename_old.c_str();

    std::string new_key_uuid;
    new_key_uuid.append(puuid);
    new_key_uuid.append(":");
    new_key_uuid.append(new_name);

    std::string old_key_uuid;
    old_key_uuid.append(puuid);
    old_key_uuid.append(":");
    old_key_uuid.append(old_name);

    //LOG(INFO)<<"old_key_uuid="<<old_key_uuid;
    //LOG(INFO)<<"new_key_uuid="<<new_key_uuid;

    /**
     * update path->uuid
     */
    if (uuid_store->remove(old_key_uuid) == false || uuid_store->setValue(new_key_uuid, uuid) == false) {
        //LOG(WARNING)<<"dir rename failed";
        return -1;
    }
    //LOG(INFO)<<"dir rename update path->uuid success";

    /**
     * update dir's stat
     */
    DirectoryInode dii;
    getValue(uuid, dii);
    dii.status=RENAMED;
    dii.ctime = time(NULL);
    setValue(uuid, dii);

    /**
     * update entry
     */
    if (ec_store->remove_entry(puuid, old_name)<0) {
        //LOG(WARNING)<<uuid<<">"<<old_name<<" remove_entry failed";
        return -1;
    }
    //LOG(INFO)<<uuid<<">"<<old_name<<" remove_entry success";
    if (ec_store->insert_entry(puuid, new_name)<0) {
        //LOG(WARNING)<<puuid<<">"<<new_name<<" insert_entry failed";
        return -1;
    }
    //LOG(INFO)<<puuid<<">"<<new_name<<" remove_entry success";
    //LOG(INFO)<<"dir rename update entry success";
	return 0;
}
void DMStore::getAttr(DirectoryInode& _return, const std::string& path, const DirectoryInode& di){
    std::string uuid;
    if (get_uuid(path, uuid) < 0) {
        _return.error = -1;
    }
    if(getValue(uuid, _return)<0){
        //LOG(WARNING)<<path<<" get attribute failed";
        _return.error = -1;
    }
    else{
    	_return.ctime = time(NULL);
		setValue(uuid, _return);
	    _return.error = 0;
    }
}
void DMStore::readdir(std::string& _return, const std::string& path){
    std::string uuid;
    get_uuid(path, uuid);
    _return=ec_store->readdir(uuid);
}
int32_t DMStore::opendir(const std::string& path, const DirectoryInode& di){
    std::string uuid;
    if (get_uuid(path, uuid) < 0) {
        return -1;
    }
    DirectoryInode dii;
    return getValue(uuid,dii);
}

int32_t DMStore::get_uuid(const std::string& path, std::string& uuid) {
    //LOG(INFO)<<"********path="<<path;
    std::vector<std::string> vbuf;
    if (path.size() != 0)
    {
        /**
         * for /a/b/c
         * from v[1] to v[3]
         */
        boost::split(vbuf, path, boost::is_any_of("/"), boost::token_compress_on);
    }
    std::string key;
    std::string value;
    key = "/";

    if (path == "/") {
        uuid = "0";
    } else {
        value = "0";
        for (int i = 1; i < vbuf.size(); ++i)
        {
            key = value+":"+vbuf[i];
            //LOG(INFO)<<"key="<<key;
            if (uuid_store->getValue(key, value) == false) {
                //LOG(WARNING)<<key<<" not exists";
                return -1;
            }
            //LOG(INFO)<<"get_uuid="<<value;
        }
        uuid = value;
    }
    //LOG(INFO)<<"********uuid="<<uuid;
    return 0;
}

int32_t DMStore::get_2_uuid(const std::string& path, std::string& puuid, std::string& uuid) {
    //LOG(INFO)<<"********path="<<path;
    std::vector<std::string> vbuf;
    if (path.size() != 0)
    {
        /**
         * for /a/b/c
         * from v[1] to v[3]
         */
        boost::split(vbuf, path, boost::is_any_of("/"), boost::token_compress_on);
    }
    std::string key;
    key = "/";

    if (path == "/") {
        puuid = "0"; uuid = "0";
    } else {
        puuid = "0"; uuid = "0";
        for (int i = 1; i < vbuf.size(); ++i)
        {
            puuid = uuid;
            key = uuid+":"+vbuf[i];
            //LOG(INFO)<<"key="<<key;
            if (uuid_store->getValue(key, uuid) == false) {
                //LOG(WARNING)<<key<<" not exists";
                return -1;
            }
            //LOG(INFO)<<"get_uuid="<<uuid;
        }
    }
    //LOG(INFO)<<"********puuid="<<puuid<<"********uuid="<<uuid;
    return 0;
}
