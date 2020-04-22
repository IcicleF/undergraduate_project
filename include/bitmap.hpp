#if !defined(BITMAP_HPP)
#define BITMAP_HPP

#include <cstdint>
#include <atomic>
#include <type_traits>

template <int NBits> struct Bits2Type     { };
template <>          struct Bits2Type<8>  { using type = std::uint8_t;  };
template <>          struct Bits2Type<16> { using type = std::uint16_t; };
template <>          struct Bits2Type<32> { using type = std::uint32_t; };
template <>          struct Bits2Type<64> { using type = std::uint64_t; }; 

template <typename Int>
inline typename std::enable_if<std::is_integral<Int>::value && (sizeof(Int) <= sizeof(int)), int>::type ffs(Int x)
{
    return __builtin_ffs(x) - 1;
}

template <typename Int>
inline typename std::enable_if<std::is_integral<Int>::value && (sizeof(Int) > sizeof(int)), int>::type ffs(Int x)
{
    return __builtin_ffsl(x) - 1;
}

template <int NBits>
class Bitmap
{
public:
    Bitmap() = default;
    ~Bitmap() = default;

    int allocBit()
    {
        BitmapTy origin = bitmap.load();
        while (origin) {
            BitmapTy lowbit = origin & -origin;
            if (bitmap.compare_exchange_weak(origin, origin & ~lowbit))
                return ffs(lowbit);
        }
        return -1;
    }

    void freeBit(int idx)
    {
        BitmapTy bit = static_cast<BitmapTy>(1) << idx;
        while (true) {
            BitmapTy origin = bitmap.load();
            if (bitmap.compare_exchange_weak(origin, origin | bit))
                return;
        }
    }
    
    using BitmapTy = typename Bits2Type<NBits>::type;
    std::atomic<BitmapTy> bitmap;
};

#endif // BITMAP_HPP
