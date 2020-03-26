/******************************************************************
 * This file is part of Galois.                                   *
 *                                                                *
 * Galois: Highly-available NVM Distributed File System           *
 * Copyright (c) 2020 Storage Research Group, Tsinghua University *
 ******************************************************************/

#if !defined(DATABLOCK_HPP)
#define DATABLOCK_HPP

#include "commons.hpp"
#include "config.hpp"
#include "debug.hpp"

/* Wraps a byte array into a block. */
template <int BlkSize>
struct DataBlock
{
    static const int capacity = BlkSize;
    static const int size = BlkSize;
    uint8_t data[BlkSize];
} __packed;

using Block512B = DataBlock<512>;
using Block1K = DataBlock<1024>;
using Block2K = DataBlock<2048>;
using Block4K = DataBlock<4096>;

/**
 * Manages a memory area as a block pool.
 * Does not manage any metadata since that is MDS's job.
 */
template <typename Ty>
class BlockPool
{
public:
    static const size_t valueSize = sizeof(Ty);

    explicit BlockPool()
    {
        if (memConf == nullptr) {
            d_err("memConf should have been initialized!");
            exit(-1);
        }

        area = reinterpret_cast<uint8_t *>(memConf->getMemory());
        uint64_t areaSize = memConf->getCapacity();
        length = areaSize / valueSize;
    }
    ~BlockPool() = default;

    /* Returns the pointer to the item with the designated index. */
    __always_inline Ty *at(uint64_t index) const
    {
        return reinterpret_cast<Ty *>(area) + index;
    }

    /* Returns the item shift related to the beginning of the bitmap */
    __always_inline uint64_t getShift(uint64_t index) const
    {
        return (uint64_t)at(index) - (uint64_t)area;
    }

    /* Returns the capacity of the allocation table */
    __always_inline uint64_t getCapacity() const { return length; }

private:
    uint8_t *area = nullptr;          /* Base pointer */
    uint64_t length = 0;              /* # of usable blocks */
};



#endif // DATABLOCK_HPP
