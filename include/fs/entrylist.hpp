/******************************************************************
 * This file is part of Galois (modified from LocoFS).            *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(ENTRYLIST_HPP)
#define ENTRYLIST_HPP

#include "../commons.hpp"

class EntryList
{
public:
    explicit EntryList() = default;
    ~EntryList() = default;
    bool insert(const std::string &uuid, const std::string &filename);
    bool remove(const std::string &uuid, const std::string &filename);
    std::string readdir(const std::string &uuid);

private:
    typedef std::unordered_set<std::string> FileList;
    std::unordered_map<std::string, FileList> entry;
    std::mutex mut;
};

#endif  // ENTRYLIST_HPP
