#include <fs/EntryList.h>

#include <boost/algorithm/string.hpp>

int EntryList::insert_entry(const std::string & uuid, const std::string & file_name){
    //LOG(INFO)<<__FUNCTION__<<" uuid: "<<uuid<<" file name: "<<file_name;
    //entry_list::iterator mit = entry.find(uuid);
    pthread_mutex_lock(m_mutex);
    entry_list::iterator mit = entry.find(uuid);
    if(mit!=entry.end()){
        //LOG(INFO)<<__FUNCTION__<<" directory exist";
        mit->second.push_back(file_name);
    }else{
        file_list  fl;
        fl.push_back(file_name);
        //LOG(INFO)<<__FUNCTION__<<" directory empty";
        entry.insert(make_pair(uuid,fl));
	    //LOG(INFO)<<"EntryList insert sucess";
    }
    pthread_mutex_unlock(m_mutex);

    return 0;
}
std::string EntryList::readdir(const std::string & uuid){
    entry_list::iterator mit = entry.find(uuid);
    if(mit!=entry.end()){
        file_list fl = mit->second;
        return boost::join(fl, "\t");
    }else
        return "";
}
int EntryList::remove_entry(const std::string & uuid, const std::string & file_name){
    //LOG(INFO)<<"remove_entry: "<<"uuid="<<uuid<<" filename="<<file_name;
    entry_list::iterator mit = entry.find(uuid);
    pthread_mutex_lock(m_mutex);
    if(mit!=entry.end()){
    //LOG(INFO)<<"find uuid="<<mit->first;
//	std::vector<std::string>::iterator it;
//	for (it=mit->second.begin(); it!=mit->second.end(); it++) {
//		//LOG(INFO)<<*it;
//	}
//	it = std::remove(mit->second.begin(), mit->second.end(), file_name);	
//	//LOG(INFO)<<"Remove: "<<*it;
//	mit->second.erase(it);
        mit->second.erase(std::remove(mit->second.begin(), mit->second.end(), file_name));
    }
    pthread_mutex_unlock(m_mutex);

    // //LOG(INFO)<<__FUNCTION__<<"uuid="<<uuid<<" file_name="<<file_name;
    // entry_list::iterator mit = entry.find(uuid);

    // pthread_mutex_lock(m_mutex);
    // if(mit!=entry.end()){
    //     mit->second.erase(std::remove(mit->second.begin(), mit->second.end(), file_name));
    // }
    // pthread_mutex_unlock(m_mutex);

    // // if(mit!=entry.end()){
    // //     file_list::iterator fl = std::find(mit->second.begin(), mit->second.end(), file_name);
    // //     if (fl != mit->second.end()) {
    // //         //LOG(INFO)<<"find file entry "<<*fl;
    // //         pthread_mutex_lock(m_mutex);
    // //         mit->second.erase(fl);
    // //         pthread_mutex_unlock(m_mutex);
    // //     } else {
    // //         //LOG(WARNING)<<"cannot find file's entry";
    // //         return -1;
    // //     }
    // // }
    return 0;
}
