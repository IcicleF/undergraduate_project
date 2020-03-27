#ifndef LocoFS_EntryList_H
#define LocoFS_EntryList_H

#include <pthread.h>

#include <boost/unordered_map.hpp>

class EntryList{
    typedef std::vector<std::string> file_list;
    typedef boost::unordered_map<std::string, file_list> entry_list;
    entry_list entry;
    pthread_mutex_t *m_mutex;
public:
    EntryList(){
    	m_mutex = new pthread_mutex_t;
    	pthread_mutex_init(m_mutex, NULL);
    }
    ~EntryList(){
    	if(m_mutex!=NULL){
    		pthread_mutex_destroy(m_mutex);
    	}
    }
    int insert_entry(const std::string & uuid, const std::string & file_name);
    std::string readdir(const std::string & uuid);
    int remove_entry(const std::string & uuid, const std::string & file_name);
};
#endif // LocoFS_EntryList_H
