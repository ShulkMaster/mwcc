#ifndef COMPILER_IROBITVECT_H
#define COMPILER_IROBITVECT_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct BitVector {
    UInt32
        size; /* 0x00: IroBitVect_AllocateBitVector sets the number of 32-bit words; IRO_BitVectorSet bounds-checks it. */
    UInt32 bits
        [1]; /* 0x04: IroBitVect_AllocateBitVector clears words; IroVars_CheckVariablesInitializedBeforeUse tests and sets variable-index bits. */
};
#pragma pack(pop)
extern void IroBitVect_ClearBitVector(BitVector *vector);
extern int IroBitVect_ContainsSubset(BitVector *subset, BitVector *superset);
extern unsigned int is_bitvector_empty(struct BitVector *values);
extern void IroBitVect_SetAllBits(BitVector *array);
extern void IroBitVect_CopyBitVector(BitVector *source, BitVector *destination);
extern void IroBitVect_Subtract(BitVector *source, BitVector *destination);
extern int IroBitVect_AreEqual(BitVector *a, BitVector *b);
extern unsigned int IroBitVect_Intersects(BitVector *left, BitVector *right);
extern unsigned int IroBitVect_AllocateBitVector(BitVector **vector, unsigned int bitCount);
extern void IroBitVect_ClearBit(UInt32 bit, BitVector *vector);
extern void IroBitVect_Or(BitVector *sourceData, BitVector *destinationData);
extern void IroBitVect_Intersect(BitVector *source, BitVector *destination);

#ifdef __cplusplus
}
#endif

#endif
