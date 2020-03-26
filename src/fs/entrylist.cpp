/******************************************************************
 * This file is part of Galois (modified from LocoFS).            *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#include <boost/algorithm/string/join.hpp>
#include <fs/entrylist.hpp>

using namespace std;

bool EntryList::insert(const string& uuid, const string& filename)
{
    unique_lock<mutex> lock(mut);
    return entry[uuid].insert(filename).second;
}

bool EntryList::remove(const string& uuid, const string& filename)
{
    unique_lock<mutex> lock(mut);
    auto mit = entry.find(uuid);
    if (mit != entry.end())
        return mit->second.erase(filename);
    return false;
}

string EntryList::readdir(const string& uuid)
{
    auto mit = entry.find(uuid);
    if (mit != entry.end()) 
        return boost::join(mit->second, "\t");
    return "";
}
