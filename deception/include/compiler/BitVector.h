#include "compiler/common.h"
#include "compiler/CError.h"
#include "compiler/IroBitVect.h"
#ifndef COMPILER_BITVECTOR_H
#define COMPILER_BITVECTOR_H

/* The optimizer's bit vectors. Each file that uses these gets them inlined, asserting with this header's name; a call
   that is not inlined goes to the one copy the linker keeps (0x00459910). */
inline void IROUseDef_SetBit(UInt32 bit, BitVector *bv)
{
    if ((bit >> 5) < bv->size)
        bv->bits[bit >> 5] |= 1u << bit;
    else
        CError_Internal("BitVector.h", 47);
}

#endif
