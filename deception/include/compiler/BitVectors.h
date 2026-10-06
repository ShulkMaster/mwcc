#ifndef COMPILER_BITVECTORS_H
#define COMPILER_BITVECTORS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CodeMotion_AllocateBits(UInt32 *destination, const UInt32 *source, SInt32 bit_count);
extern SInt32 BitVectors_CopyAndCheckChanged(UInt32 *destination, UInt32 *source, SInt32 bitCount);

#ifdef __cplusplus
}
#endif

#endif
