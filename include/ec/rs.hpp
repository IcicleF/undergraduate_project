#if !defined(RS_HPP)
#define RS_HPP

#include "ec.hpp"
#include <isa-l.h>

template <unsigned K, unsigned P, unsigned Len>
class ReedSolomonCode : public ErasureCodingBase
{
public:
    /**
     * @brief Encode parity block from original data.
     * 
     * @param srcData       An (K * Len) 2d-array containing original data.
     * @param outputData    An (P * Len) 2d-array allocated space.
     */
    static void encode(uint8_t **srcData, uint8_t **outputData)
    {
        auto *inst = instance();
        ec_encode_data(Len, K, P, inst->gfTables, srcData, outputData);
    }

    /**
     * @brief Decode original data from provided data blocks.
     * 
     * @param srcId         An 1d-array containing IDs of fetched data blocks.
     * @param data          An ((K + P) * Len) 2d-array, whose lines with IDs in `srcId` are
     *                      filled with fetched data blocks.
     * @return              Fills empty lines (ID not in `srcId`) of data[0 .. K-1].
     */
    static void decode(int *srcId, uint8_t **data)
    {
        uint8_t decodeMatrix[N * K];            /* Decode matrix */
        uint8_t invertMatrix[K * K];            /* Inversion of `b` */
        uint8_t b[K * K];                       /* Extracted lines from encode matrix */
        uint8_t *recoverSrc[K];                 /* Points to source data */
        uint8_t *recoverOutput[P];              /* Points to recovered data */
        int errs = 0;                           /* Number of corrupted sources */
        int errIndex[P];                        /* Indexes of corrupted sources */
        bool live[N];                           /* Liveness bitmap */

        auto *inst = instance();

        for (int i = 0; i < N; ++i)
            live[i] = 0;
        for (int i = 0; i < K; ++i) {
            live[srcId[i]] = 1;
            recoverSrc[i] = data[srcId[i]];
        }
        for (int i = 0; i < N; ++i)
            if (!live[i]) {
                errIndex[errs] = i;
                recoverOutput[errs++] = data[srcId[i]]; 
            }

        for (int i = 0; i < K; ++i)
            for (int j = 0; j < K; ++j)
                b[K * i + j] = inst->encodeMatrix[K * srcId[i] + j];
        gf_invert_matrix(b, invertMatrix, K);

        for (int i = 0; i < errs; ++i)
            memcpy(&decodeMatrix[K * i], &invertMatrix[K * errIndex[i]], K);
    
        uint8_t gfTbls[K * P * 32];
        ec_init_tables(K, errs, decodeMatrix, gfTbls);
        ec_encode_data(Len, K, errs, gfTbls, recoverSrc, recoverOutput);
    }

    static std::string desc()
    {
        using std::to_string;
        return "RS(" + to_string(K) + ", " + to_string(P) + "), Len = " + to_string(Len);
    }

private:
    static const unsigned N = K + P;

    explicit ReedSolomonCode()
    {
        gf_gen_cauchy1_matrix(encodeMatrix, N, K);
        ec_init_tables(K, P, &encodeMatrix[K * K], gfTables);
    }
    ~ReedSolomonCode() { }

    static inline ReedSolomonCode<K, P, Len> *instance()
    {
        static ReedSolomonCode<K, P, Len> *inst = nullptr;
        if (!inst)
            inst = new ReedSolomonCode<K, P, Len>;
        return inst;
    }

    uint8_t encodeMatrix[N * K];
    uint8_t gfTables[K * P * 32];
};

#endif // RS_HPP
