#if !defined(ALLOCTABLE_HPP)
#define ALLOCTABLE_HPP

#include "common.hpp"
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

        auto *area = reinterpret_cast<uint8_t *>(memConf->getDataArea());
        uint64_t areaSize = memConf->getDataAreaCapacity();
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
            mem_force_flush(allocTableMagic);                       /* persist */
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

    /*
     * Test a bit in the allocation table bitmap. 
     * 1 - vacant
     * 0 - occupied
     */
    __always_inline int testBit(uint64_t index)
    {
        register int ret asm("eax");
        asm volatile(
            "btq %2, %3;"
            "setb %%al"
            : "=r"(ret)
            : "0"(0), "r"(index), "m"(*(bitmap))
            : "cc"
        );
        return ret;
    }

    /* Atomically test and set bit (deallocate). */
    __always_inline int setBit(uint64_t index)
    {
        register int ret asm("eax");
        asm volatile(
            "lock btsq %2, %3;"
            "setb %%al"
            : "=r"(ret)
            : "0"(0), "r"(index), "m"(*(bitmap))
            : "cc"
        );
        return ret;
    }

    /* Atomically test and clear bit (allocate). */
    __always_inline int clearBit(uint64_t index)
    {
        register int ret asm("eax");
        asm volatile(
            "lock btrq %2, %3;"
            "setb %%al"
            : "=r"(ret)
            : "0"(0), "r"(index), "m"(*(bitmap))
            : "cc"
        );
        return ret;
    }

    /* Find the first 1 (vacant) bit in the allocation table bitmap, starting from the designate bit. */
    __always_inline uint64_t findVacantBit(uint64_t startFrom)
    {
        register uint64_t ret asm("rax");

        if (startFrom < 0 || startFrom >= length)
            return -1;
        startFrom &= -8;                                // round down to whole bytes

        asm volatile(
            "bsf %1, %0"
            : "=r"(ret)
            : "m"(*(bitmap + startFrom / 8))
        );
        ret += startFrom;

        if (ret >= length)
            return -1;
        return ret;
    }

    /* */
    Ty *allocElem(uint64_t *index)
    {
        uint64_t ret = 0;

        while (1) {
            ret = findVacantBit(ret);
            if (unlikely(ret == -1)) {
                *index = -1;
                return NULL;
            }
            if (likely(clearBit(ret) == 1))     /* If successfully cleared the 1, returns */
                break;
        }

        ++allocated;
        *index = ret;
        return at(*index);
    }
    
    void deallocElem(Ty *elem)
    {
        deallocElem(elem - reinterpret_cast<Ty *>(mappedArea));
    }
    void deallocElem(uint64_t index)
    {
        if (unlikely(setBit(index) == 1)) {
            d_err("bit #%lu is already vacant", index);
            return;
        }
        --allocated;
    }


private:
    uint32_t *allocTableMagic = nullptr;    /* Detect if the pmem has been initialized */
    uint8_t *bitmap = nullptr;              /* Bitmap array start location */
    void *mappedArea = nullptr;             /* Mapped area start location */
    /* Above members points to locations in NVM */

    uint64_t length = 0;                    /* Bitmap length in bits (equivalent to # of blocks) */
    uint64_t allocated = 0;                 /* Allocated elements */
};



#endif // ALLOCTABLE_HPP
