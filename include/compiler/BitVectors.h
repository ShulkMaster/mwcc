#ifndef COMPILER_BITVECTORS_H
#define COMPILER_BITVECTORS_H

#include "compiler/common.h"
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VERSION >= VERSION_GC_2_7
extern void bitvectorinitialize(UInt32 *words, int bitCount, unsigned int value);
extern void bitvectorunion(UInt32 *destination, const UInt32 *source, int bitCount);
extern void bitvectorcopy(UInt32 *destination, const UInt32 *source, int bitCount);
#endif
extern void CodeMotion_AllocateBits(UInt32 *destination, const UInt32 *source, SInt32 bit_count);
extern SInt32 BitVectors_CopyAndCheckChanged(UInt32 *destination, UInt32 *source, SInt32 bitCount);

#ifdef __cplusplus
}
#endif

#endif
