#include <fs/FMStore.h>

#include <boost/algorithm/string.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

int32_t FMStore::getValue(const std::string &Key, FileAccessInode &fa)
{
    std::string value;
    if (file_access_store->getValue(Key, value) == false) {
        //LOG(WARNING)<<Key<<" get FileAccessInode failed";
        return -1;
    }
    //LOG(INFO)<<Key<<" get FileAccessInode success:"<<" size="<<value.size()<<" FileAccessInode="<<value;
    std::istringstream is(value);
    boost::archive::text_iarchive ia(is);
    ia >> fa;
    return 0;
}
int32_t FMStore::setValue(const std::string &Key, const FileAccessInode &fa)
{
    std::ostringstream os;
    boost::archive::text_oarchive oa(os);
    oa << fa;
    std::string value = os.str();
    if (file_access_store->setValue(Key, value) == false) {
        //LOG(WARNING)<<Key<<"set FileAccessInode failed"<<" size="<<value.size()<<" FileAccessInode="<<value;
        return -1;
    }
    //LOG(INFO)<<Key<<" set FileAccessInode success:"<<" size="<<value.size()<<" FileAccessInode="<<value;
    return 0;
}
int32_t FMStore::getValue(const std::string &Key, FileContentInode &fc)
{
    std::string value;
    if (file_content_store->getValue(Key, value) == false) {
        //LOG(WARNING)<<Key<<"get FileContentInode failed";
        return -1;
    }
    //LOG(INFO)<<Key<<" get FileContentInode success:"<<" size="<<value.size()<<" FileContentInode="<<value;
    std::istringstream is(value);
    boost::archive::text_iarchive ia(is);
    ia >> fc;
    return 0;
}
int32_t FMStore::setValue(const std::string &Key, const FileContentInode &fc)
{
    std::ostringstream os;
    boost::archive::text_oarchive oa(os);
    oa << fc;
    std::string value = os.str();
    if (file_content_store->setValue(Key, value) == false) {
        //LOG(WARNING)<<Key<<"set FileContentInode failed"<<" size="<<value.size()<<" FileContentInode="<<value;
        return -1;
    }
    //LOG(INFO)<<Key<<" set FileContentInode success:"<<" size="<<value.size()<<" FileContentInode="<<value;
    return 0;
}

int32_t FMStore::getValue(const std::string &Key, FileInode &fi)
{
    if (getValue(Key, fi.fa) < 0) {
        return -1;
    }
    if (getValue(Key, fi.fc) < 0) {
        return -1;
    }
    return 0;
}
int32_t FMStore::setValue(const std::string &Key, const FileInode &fi)
{
    if (setValue(Key, fi.fa) < 0) {
        return -1;
    }
    if (setValue(Key, fi.fc) < 0) {
        return -1;
    }
    return 0;
}
/**
 * [FMStore::create description]
 * @param  path Key
 * @param  fa   [description]
 * @return      [description]
 */
int32_t FMStore::create(const std::string &Key, const FileAccessInode &fa)
{
    FileAccessInode faa;
    faa.mode = fa.mode;
    faa.gid = fa.gid;
    faa.uid = fa.uid;
    FileContentInode fcc;
    fcc.origin_name = Key;
    fcc.block_size = 4096;
    fcc.size = 0;
    fcc.suuid = suuid_counter++;
    fcc.sid = sid_num;

    /**
     *  kv store fileinode
     */
    //LOG(INFO)<<__FUNCTION__<<" "<<Key<<" Begin Set File Access Inode";
    if (setValue(Key, faa) < 0) {
        return -1;
    }
    //LOG(INFO)<<__FUNCTION__<<" "<<Key<<" Begin Set File Content Inode";
    if (setValue(Key, fcc) < 0) {
        return -1;
    };
    /**
     *  insert entry
     */
    std::vector<std::string> vkey;
    if (strlen(Key.c_str()) != 0) {
        boost::split(vkey, Key, boost::is_any_of(":"), boost::token_compress_on);
    }
    else {
        //LOG(ERROR)<<__FUNCTION__<<" "<<Key<<" empty Key";
    }
    if (vkey.size() == 2) {
        ec_store->insert_entry(vkey[0], vkey[1]);
        //LOG(INFO)<<__FUNCTION__<<" "<<Key<<" entry inserted";

        /**
         * This is for "Store Entry as obj" modification
         */
        // std::string buf;
        // buf = ec_store->readdir(vkey[0]);
        // std::hash<std::string> hash_str;
        // if (data_trans[hash_str(vkey[0])%data_trans.size()].write(vkey[0], 4096, 0, buf) < 0)
        // {
        //     //LOG(ERROR)<< vkey[0] << " Write Entry Obj fail";
        //     return false;
        // }
        // //LOG(ERROR)<< vkey[0] << " Write Entry Obj Success";
    }
    else {
        //LOG(ERROR)<<__FUNCTION__<<" "<<Key<<" bad Key";
    }
    return 0;
}

int32_t FMStore::access(const std::string &Key, const FileAccessInode &fa)
{
    FileAccessInode faa;
    return getValue(Key, faa);
}
int32_t FMStore::chown(const std::string &Key, const FileAccessInode &fa)
{
    FileAccessInode faa;
    getValue(Key, faa);
    faa.uid = fa.uid;
    faa.gid = fa.gid;
    setValue(Key, faa);
    return 0;
}
int32_t FMStore::chmod(const std::string &Key, const FileAccessInode &fa)
{
    FileAccessInode faa;
    getValue(Key, faa);
    faa.mode = fa.mode;
    setValue(Key, faa);
    return 0;
}
int32_t FMStore::remove(const std::string &Key, const FileAccessInode &fa)
{
    /**
     *  kv remove fileinode
     */
    if (file_access_store->remove(Key) == false) {
        return -1;
    }
    if (file_content_store->remove(Key) == false) {
        return -1;
    }
    /**
     *  remove entry
     */
    std::vector<std::string> vkey;
    boost::split(vkey, Key, boost::is_any_of(":"), boost::token_compress_on);
    ec_store->remove_entry(vkey[0], vkey[1]);

    return 0;
}
int32_t FMStore::csize(const std::string &Key, const FileContentInode &fc)
{
    FileContentInode fcc;
    if (getValue(Key, fcc) < 0)
        return -1;
    //LOG(INFO) << __FUNCTION__ << " Key: " << Key << " size: " << fc.size;
    fcc.size = fc.size;
    if (setValue(Key, fcc) < 0)
        return -1;
    return 0;
}
void FMStore::getAttr(FileInode &_return, const std::string &Key, const FileInode &fi)
{
    if (getValue(Key, _return) < 0) {
        //LOG(ERROR) << Key << " getattr failed";
        _return.error = -1;
    }
    else {
        setValue(Key, _return);
        _return.error = 0;
        //LOG(INFO) << __FUNCTION__ << " Key: " << Key;
    }
}
void FMStore::getContent(FileContentInode &_return, const std::string &Key, const FileContentInode &fi)
{
    if (getValue(Key, _return) < 0) {
        _return.error = -1;
    }
    else {
        setValue(Key, _return);
        _return.error = 0;
        //LOG(INFO) << __FUNCTION__ << " Key: " << Key;
    }
}
void FMStore::getAccess(FileAccessInode &_return, const std::string &Key, const FileAccessInode &fa)
{
    if (getValue(Key, _return) < 0) {
        _return.error = -1;
    }
    else {
        _return.error = 0;
    }
}
void FMStore::readdir(std::string &_return, const int64_t uuid)
{
    _return = ec_store->readdir(std::to_string(uuid));
}
int32_t FMStore::utimens(const std::string &Key, const FileContentInode &fc)
{
    FileContentInode fcc;
    if (getValue(Key, fcc) < 0) {
        return -1;
    }
    if (setValue(Key, fcc) < 0)
        return -1;
    return 0;
}
void FMStore::open(FileInode &_return, const std::string &Key, const FileAccessInode &fa)
{
    if (getValue(Key, _return) < 0) {
        _return.error = -1;
    }
    else {
        _return.fa.mode = fa.mode;
        _return.error = 0;
    }
}
int32_t FMStore::rename(const std::string &old_path, const std::string &new_path)
{
    /**
     * to-do
     */
    return 0;
}
