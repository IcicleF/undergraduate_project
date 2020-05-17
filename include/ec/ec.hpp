#if !defined(EC_HPP)
#define EC_HPP

#include <cstdint>
#include <string>

class ErasureCodingBase
{
public:
    static void encode(uint8_t **srcData, uint8_t **outputData) { }
    static void decode(int *srcId, uint8_t **data) { }
    static std::string desc() { return "False EC Strategy"; }

protected:
    ErasureCodingBase() = default;
    ~ErasureCodingBase() = default;
};

#endif // EC_HPP
