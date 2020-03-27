/***************************************
 * function: local lru cache
 * author: liuyi
 * date: 2015.10.24
 * version: 1.0
 * ****************************************/

#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include <stdlib.h>
//#include <iostream>
#include <map>
#include <time.h>
#include <pthread.h>
using namespace std;

template<class T1, class T2>
class lru_cache
{
    public:
        struct cache_data
        {
            time_t begin_time;
            time_t update_time;
            int expire_time;
            int count;
            T2 data;
        };

        lru_cache()
        {
            m_max_size = 0;
            m_avg_expire_time = 0;
            m_mutex = NULL;
            m_cache_map = NULL;
        }

        ~lru_cache()
        {
            m_max_size = 0;
            if(NULL != m_cache_map)
            {
                delete m_cache_map;
                m_cache_map = NULL;
            }

            if(m_mutex != NULL)
            {
                pthread_mutex_destroy(m_mutex);
            }
        }

        bool init(int max_cache_size)
        {
            m_mutex = new pthread_mutex_t;
            m_cache_map = new map<T1, cache_data>;
            m_max_size = max_cache_size;
            pthread_t id;
            pthread_mutex_init(m_mutex, NULL);
            pthread_create(&id, NULL, expire_thread, this);
            return true;
        }

        bool get(const T1& key, T2& data)
        {
            pthread_mutex_lock(m_mutex);
            typename map<T1, cache_data>::iterator it = m_cache_map->find(key);
            if(it == m_cache_map->end())
            {
                pthread_mutex_unlock(m_mutex);
                return false;
            }

            data = it->second.data;
            it->second.count++;
            it->second.update_time = time(NULL);
            pthread_mutex_unlock(m_mutex);
            return true;
        }

        bool set(const T1& key, const T2& data, const int& expire_time = -1)
        {
            cache_data new_data;
            new_data.count = 0;
            new_data.expire_time = expire_time;
            new_data.begin_time = time(NULL);
            new_data.update_time = new_data.begin_time;
            new_data.data = data;
            pthread_mutex_lock(m_mutex);
            if(m_cache_map->size() >= m_max_size)
            {
                pthread_mutex_unlock(m_mutex);
                lru_remove();
                pthread_mutex_lock(m_mutex);
            }
            m_cache_map->insert(pair<T1, cache_data>(key, new_data));
            pthread_mutex_unlock(m_mutex);
            return true;
        }

        bool remove(const T1& key)
        {
            pthread_mutex_lock(m_mutex);
            if(m_cache_map->find(key) != m_cache_map->end())
            {
                m_cache_map->erase(key);
                pthread_mutex_unlock(m_mutex);
                return true;
            }

            pthread_mutex_unlock(m_mutex);
            return false;
        }

        bool lru_remove()
        {
            pthread_mutex_lock(m_mutex);
            typename map<T1, cache_data>::iterator lru_it = m_cache_map->begin();
            typename map<T1, cache_data>::iterator it = m_cache_map->begin();
            if(it == m_cache_map->end())
            {
                pthread_mutex_unlock(m_mutex);
                return false;
            }

            size_t weight = it->second.count + it->second.update_time;
            for(++it; it != m_cache_map->end(); ++it)
            {
                if(it->second.count + it->second.update_time < weight)//lru
                {
                    weight = it->second.count + it->second.update_time;
                    lru_it = it;
                }
            }
            m_cache_map->erase(lru_it);
            pthread_mutex_unlock(m_mutex);
            return true;
        }

        int lru_expire()
        {
            int count = 0;
            pthread_mutex_lock(m_mutex);
            long long sum = 0;
            for(typename map<T1, cache_data>::iterator it = m_cache_map->begin();
                it != m_cache_map->end();)
            {
                sum += it->second.expire_time;
                if(it->second.expire_time >= 0
                   && time(NULL) - it->second.update_time >= it->second.expire_time)
                {
                    m_cache_map->erase(it++);
                    ++count;
                }
                else
                {
                    ++it;
                }
            }

            if(!m_cache_map->empty())
            {
                m_avg_expire_time = sum / m_cache_map->size();
            }
            pthread_mutex_unlock(m_mutex);

            return count;
        }

        int get_avg_expire_time()const
        {
            pthread_mutex_lock(m_mutex);
            int avg_expire_time = m_avg_expire_time;
            pthread_mutex_unlock(m_mutex);
            return avg_expire_time;
        }

    private:
        static void* expire_thread(void *args)
        {
            lru_cache *p_instance = (lru_cache *)args;
            for(;;)
            {
                sleep(abs(p_instance->get_avg_expire_time()/2));
                p_instance->lru_expire();
            }
            return NULL;
        }

    private:
        int m_max_size;
        int m_avg_expire_time;
        map<T1, cache_data> *m_cache_map;
        pthread_mutex_t *m_mutex;
};

#endif // LRU_CACHE_H
