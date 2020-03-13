/*
 * hashtable.hpp
 * 
 * Copyright (c) 2020 Storage Research Group, Tsinghua University
 * 
 * A lightweight hash table for RDMA work request ID allocation
 * and notification of completion.
 * 
 * Some special types of requests have special IDs.
 */

#if !defined(HASHTABLE_HPP)
#define HASHTABLE_HPP

#include <random>

#include "commons.hpp"
#include "debug.hpp"

class LinearHashTable;
class MapHashTable;

enum SpecialHash
{
    SP_REMOTE_MR_SEND = 1,
    SP_REMOTE_MR_RECV,
    SP_TYPES
};

struct HashTableEntry
{
    bool complete;
    enum Type { HTE_SEND_REQ, HTE_RECV_REQ, HTE_READ_REQ, HTE_WRITE_REQ } type;
    uint64_t imm;
};

class LinearHashTable
{
public:
    explicit LinearHashTable() : rnd(SP_TYPES, maxEntries - 1)
    {
        memset(bitmap, 0, maxEntries >> 3);
    }
    ~LinearHashTable() = default;

    __always_inline uint32_t allocID(HashTableEntry::Type type)
    {
        uint32_t id = rnd(engine);
        while (unlikely(setBit(id) == 1))
            id = rnd(engine);
        entries[id].complete = false;
        entries[id].type = type;
        return id;
    }
    __always_inline void freeID(uint32_t id) { clearBit(id); }
    __always_inline HashTableEntry &operator[](uint32_t id) { return entries[id]; }

private:
    /* Atomically test and set bit (allocate). */
    __always_inline int setBit(uint64_t index)
    {
        int ret;
        asm volatile(
            "lock btsq %2, %3;"
            "setb %%al"
            : "=a"(ret)
            : "0"(0), "r"(index), "m"(*(bitmap))
            : "cc"
        );
        return ret;
    }

    /* Atomically test and clear bit (deallocate). */
    __always_inline int clearBit(uint64_t index)
    {
        int ret;
        asm volatile(
            "lock btrq %2, %3;"
            "setb %%al"
            : "=a"(ret)
            : "0"(0), "r"(index), "m"(*(bitmap))
            : "cc"
        );
        return ret;
    }

    std::mt19937 engine;
    std::uniform_int_distribution<uint32_t> rnd;

    static const int maxEntries = 1 << 20;
    uint8_t bitmap[maxEntries >> 3];
    HashTableEntry entries[maxEntries];
};

class MapHashTable
{
public:
    explicit MapHashTable() : rnd(SP_TYPES, UINT32_MAX) { };
    ~MapHashTable() = default;

    __always_inline uint32_t allocID(HashTableEntry::Type type)
    {
        uint32_t id = rnd(engine);
        while (unlikely(hashMap.find(id) != hashMap.end()))
            id = rnd(engine);
        hashMap[id].complete = false;
        hashMap[id].type = type;
        return id;
    }
    __always_inline void freeID(uint32_t id) { hashMap.erase(id); }
    __always_inline HashTableEntry &operator[](uint32_t id) { return hashMap[id]; }

private:
    std::mt19937 engine;
    std::uniform_int_distribution<uint32_t> rnd;

    std::unordered_map<uint32_t, HashTableEntry> hashMap;
};

typedef LinearHashTable HashTable;

#endif // HASHTABLE_HPP
