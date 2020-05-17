#if !defined(LRC_HPP)
#define LRC_HPP

#include "ec.hpp"

template <unsigned K, unsigned N, unsigned P>
class LocalReconstructionCode : public ErasureCodingBase
{
private:
    static const unsigned Total = (K + 1) * N + P;

    explicit LocalReconstructionCode()
    {
        
    }
};

#endif // LRC_HPP
