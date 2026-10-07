#ifndef COMPILER_BITVECTORS_H
#define COMPILER_BITVECTORS_H

#include "compiler/common.h"
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void bitvectorcopy(UInt32 *destination, const UInt32 *source, int bit_count);
extern void bitvectorinitialize(UInt32 *words, int bitCount, UInt32 value);
extern void bitvectorunion(UInt32 *destination, const UInt32 *source, int bitCount);
extern SInt32 BitVectors_CopyAndCheckChanged(UInt32 *destination, UInt32 *source, SInt32 bitCount);

#ifdef __cplusplus
}
#endif

#endif
