#include <boost/algorithm/string.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/filesystem.hpp>

#include <commons.hpp>
#include <debug.hpp>
#include <fs/dms.hpp>

using namespace std;

int DMStore::getInode(const string &uuid, DirectoryInode &di)
{
    string value;
    expectTrue(dms->get("dm/" + uuid, value));

    istringstream is(value);
    boost::archive::text_iarchive ia(is);
    ia >> di;
    return 0;
}

int DMStore::setInode(const string &uuid, const DirectoryInode &di)
{
    ostringstream os;
    boost::archive::text_oarchive oa(os);
    oa << di;
    string value = os.str();

    expectTrue(dms->set("dm/" + uuid, value));
    return 0;
}

int DMStore::mkdir(const string &path, const DirectoryInode &di)
{
    uint64_t curUuid = uuidCounter.fetch_add(1);

    boost::filesystem::path _path(path);
    auto parent = _path.parent_path();
    auto filename = _path.filename();
    string pUuid;
    string keyUuid;

    if (path == "/")
        keyUuid = "/";
    else {
        if (getUuid(parent.string(), pUuid) < 0) {
            d_warn("parent directory %s does not exist", parent.string());
            return -1;
        }
        keyUuid = "dm/" + pUuid + "/" + filename.string();
    }

    string tmp;
    if (dms->get(keyUuid, tmp)) {
        d_warn("directory %s (uuid: %s) exists", path.c_str(), keyUuid.c_str());
        return -1;
    }

    tmp = to_string(curUuid);
    expectTrue(dms->set(keyUuid, tmp));
    
    DirectoryInode dii;
    dii.mode = di.mode;
    dii.gid = di.gid;
    dii.uid = di.uid;
    dii.status = ON_USE;
    dii.name = path;
    dii.uuid = curUuid;

    setInode(tmp, dii);

    if (path != "/")
        ecs->insert(pUuid, filename.string());
    return 0;
}

int DMStore::access(const string &path, const DirectoryInode &di)
{
    string uuid;
    return (getUuid(path, uuid) < 0) ? -1 : 0;
}

int DMStore::chown(const string &path, const DirectoryInode &di)
{
    string uuid;
    DirectoryInode dii;

    if (getUuid(path, uuid) < 0)
        return -1;
    
    getInode(uuid, dii);
    dii.uid = di.uid;
    dii.gid = di.gid;
    setInode(uuid, dii);

    return 0;
}

int DMStore::chmod(const string &path, const DirectoryInode &di)
{
    string uuid;
    DirectoryInode dii;
    
    if (getUuid(path, uuid) < 0)
        return -1;
    
    getInode(uuid, dii);
    dii.mode = di.mode;
    setInode(uuid, di);

    return 0;
}

int DMStore::rmdir(const string &path, const DirectoryInode &di)
{
    if (path == "/")
        return -1;
    
    string uuid, pUuid;
    boost::filesystem::path tmp(path);
    auto filename = tmp.filename();

    if (get2Uuid(path, pUuid, uuid) < 0)
        return -1;
    expectTrue(dms->remove("dm/" + uuid));
    
    string keyUuid = "uuid/" + pUuid + "/" + filename.string();
    expectTrue(dms->remove(keyUuid));
    expectTrue(ecs->remove(pUuid, filename.string()));

    return 0;
}

int DMStore::rename(const string &old_path, const string &new_path)
{
    /**
     * get old_keyUuid, new_keyUuid, uuid
     */
    string uuid;
    string pUuid;
    if (get_2_uuid(old_path, pUuid, uuid) < 0) {
        LOG(WARNING) << old_path << "no such dir";
        return -1;
    }

    boost::filesystem::path tmp_new(new_path);
    boost::filesystem::path filename_new = tmp_new.filename();
    string new_name = filename_new.c_str();

    boost::filesystem::path tmp_old(old_path);
    boost::filesystem::path filename_old = tmp_old.filename();
    string old_name = filename_old.c_str();

    string new_keyUuid;
    new_keyUuid.append(pUuid);
    new_keyUuid.append(":");
    new_keyUuid.append(new_name);

    string old_keyUuid;
    old_keyUuid.append(pUuid);
    old_keyUuid.append(":");
    old_keyUuid.append(old_name);

    LOG(INFO) << "old_keyUuid=" << old_keyUuid;
    LOG(INFO) << "new_keyUuid=" << new_keyUuid;

    /**
     * update path->uuid
     */
    if (uuid_store->remove(old_keyUuid) == false || uuid_store->setValue(new_keyUuid, uuid) == false) {
        LOG(WARNING) << "dir rename failed";
        return -1;
    }
    LOG(INFO) << "dir rename update path->uuid success";

    /**
     * update dir's stat
     */
    DirectoryInode dii;
    getValue(uuid, dii);
    dii.status = RENAMED;
    dii.ctime = time(NULL);
    setValue(uuid, dii);

    /**
     * update entry
     */
    if (ec_store->remove_entry(pUuid, old_name) < 0) {
        LOG(WARNING) << uuid << ">" << old_name << " remove_entry failed";
        return -1;
    }
    LOG(INFO) << uuid << ">" << old_name << " remove_entry success";
    if (ec_store->insert_entry(pUuid, new_name) < 0) {
        LOG(WARNING) << pUuid << ">" << new_name << " insert_entry failed";
        return -1;
    }
    LOG(INFO) << pUuid << ">" << new_name << " remove_entry success";
    LOG(INFO) << "dir rename update entry success";
    return 0;
}

void DMStore::getAttr(DirectoryInode &_return, const string &path, const DirectoryInode &di)
{
    string uuid;
    if (getUuid(path, uuid) < 0) {
        _return.error = -1;
    }
    if (getValue(uuid, _return) < 0) {
        LOG(WARNING) << path << " get attribute failed";
        _return.error = -1;
    }
    else {
        _return.ctime = time(NULL);
        setValue(uuid, _return);
        _return.error = 0;
    }
}
void DMStore::readdir(string &_return, const string &path)
{
    string uuid;
    getUuid(path, uuid);
    _return = ec_store->readdir(uuid);
}
int DMStore::opendir(const string &path, const DirectoryInode &di)
{
    string uuid;
    if (getUuid(path, uuid) < 0) {
        return -1;
    }
    DirectoryInode dii;
    return getValue(uuid, dii);
}

int DMStore::getUuid(const string &path, string &uuid)
{
    vector<string> vbuf;
    if (path.size() != 0)
        boost::split(vbuf, path, boost::is_any_of("/"), boost::token_compress_on);
    string key;
    string value;
    key = "/";

    if (path == "/")
        uuid = "0";
    else {
        value = "0";
        for (int i = 1; i < vbuf.size(); ++i) {
            key = "uuid/" + value + ":" + vbuf[i];
            if (!dms->get(key, value))
                return -1;
        }
        uuid = value;
    }
    return 0;
}

int DMStore::get_2_uuid(const string &path, string &pUuid, string &uuid)
{
    LOG(INFO) << "********path=" << path;
    vector<string> vbuf;
    if (path.size() != 0) {
        /**
         * for /a/b/c
         * from v[1] to v[3]
         */
        boost::split(vbuf, path, boost::is_any_of("/"), boost::token_compress_on);
    }
    string key;
    key = "/";

    if (path == "/") {
        pUuid = "0";
        uuid = "0";
    }
    else {
        pUuid = "0";
        uuid = "0";
        for (int i = 1; i < vbuf.size(); ++i) {
            pUuid = uuid;
            key = uuid + ":" + vbuf[i];
            LOG(INFO) << "key=" << key;
            if (uuid_store->getValue(key, uuid) == false) {
                LOG(WARNING) << key << " not exists";
                return -1;
            }
            LOG(INFO) << "getUuid=" << uuid;
        }
    }
    LOG(INFO) << "********pUuid=" << pUuid << "********uuid=" << uuid;
    return 0;
}

DEFINE_MAIN_INFO();

int main()
{
    COLLECT_MAIN_INFO();
    return 0;
}