#if !defined(ALLOCTABLE_HPP)
#define ALLOCTABLE_HPP

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
};

using Block512B = DataBlock<512>;
using Block1K = DataBlock<1024>;
using Block2K = DataBlock<2048>;
using Block4K = DataBlock<4096>;

/*
 * Memory Organization:
 * 
 * +---- dataArea
 * |
 * V
 * +-------+----------+--------------------------------+
 * | Magic |  Bitmap  |          Element pool          |
 * +-------+----------+--------------------------------+
 * \                                                  /
 *  \______________ dataAreaCapacity ________________/
 * 
 * This function automacally calculates the element pool's length (= #bits in Bitmap),
 * This length will be rounded down to times of 64.
 * 
 * Persistent memory configuration structure `memConf` should HAVE BEEN initialized.
 */
template <typename Ty>
class AllocationTable
{
public:
    static const size_t valueSize = sizeof(Ty);

    explicit AllocationTable()
    {
        if (memConf == nullptr) {
            d_err("memConf should have been initialized!");
            exit(-1);
        }

        auto *area = reinterpret_cast<uint8_t *>(memConf->getMemory());
        uint64_t areaSize = memConf->getCapacity();
        bool shouldInit = true;

        if (((uint64_t)area) % 8 != 0) {
            d_err("memory location should be divisible by 8");
            exit(-1);
        }
        if (valueSize % 8 != 0) {
            d_warn("value size should be divisible by 8");
            exit(-1);
        }

        allocTableMagic = reinterpret_cast<uint32_t *>(area);
        bitmap = area + 8;

        /* Go read the initialized allocation table */
        if (*allocTableMagic == ALLOC_TABLE_MAGIC) {
            d_info("magic indicates that the allocation table has been initialized");
            shouldInit = false;
        }
        
        uint64_t tmp = (areaSize - 8) / (valueSize * 64 + 8);       /* packaged 64 * (elem + 1b) pairs */
        uint64_t bitmapBytes = tmp * 8;

        mappedArea = bitmap + bitmapBytes;
        length = bitmapBytes * 8;

        if (shouldInit) {
            memset(bitmap, -1, bitmapBytes);                        /* Initialize to all-ones */
            __mem_clflush(allocTableMagic);                       /* persist */
        }
    }
    ~AllocationTable() = default;

    /* Returns the pointer to the item with the designated index. */
    __always_inline Ty *at(uint64_t index) const
    {
        return reinterpret_cast<Ty *>(mappedArea) + index;
    }

    /* Returns the item shift related to the beginning of the bitmap */
    __always_inline uint64_t getShift(uint64_t index) const
    {
        return (uint64_t)at(index) - (uint64_t)allocTableMagic;
    }

    /* Returns the number of allocated elements */
    __always_inline uint64_t getCount() const { return allocated; }

    /* Returns the capacity of the allocation table */
    __always_inline uint64_t getCapacity() const { return length; }

private:
    uint32_t *allocTableMagic = nullptr;    /* Detect if the pmem has been initialized */
    uint8_t *bitmap = nullptr;              /* Bitmap array start location */
    void *mappedArea = nullptr;             /* Mapped area start location */
    /* Above members points to locations in NVM */

    uint64_t length = 0;                    /* Bitmap length in bits (equivalent to # of blocks) */
    uint64_t allocated = 0;                 /* Allocated elements */
};



#endif // ALLOCTABLE_HPP
