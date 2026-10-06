#ifndef COMPILER_VECTORARRAYSTOREGS_H
#define COMPILER_VECTORARRAYSTOREGS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AggregateRecord {
    struct AggregateRecord *next;
    struct Object *object;
    unsigned char flag : 1;
    unsigned char : 7;
    int size;
    int count;
    int index;
    int slots[1];
};
struct CodeMotionBits {
    UInt32 *gen;
    UInt32 *kill;
    UInt32 *in;
    UInt32 *out;
};
#pragma pack(push, 1)
struct VectorArrayEntry {
    struct PCodeInstruction *array;
    struct VectorArrayUse *uses;
};
#pragma pack(pop)
extern void VectorArraysToRegs_ReplaceArrayUsesWithVMR(struct AggregateRecord *registerMap, int arrayIndex);
extern int fn_0052d1e0(int valueIndex, int entryIndex);
extern void VectorArraysToRegs_ComputeInOutBits(void);
extern struct AggregateRecord *VectorArraysToRegs_BuildAggregateRecords(void);
extern void VectorArraysToRegs_ComputeGenKill(struct AggregateRecord *entries);
extern void VectorArraysToRegs_BuildLoadIndexEntries(struct AggregateRecord *argument);
extern void fn_0052d8d0(struct AggregateRecord *analysis);
extern int load_index_count;
extern VectorArrayEntry *load_index_entries;
extern struct CodeMotionBits *codeMotionBits;
extern int *load_index_entry_counts;
extern int *block_entry_start;

#ifdef __cplusplus
}
#endif

#endif
