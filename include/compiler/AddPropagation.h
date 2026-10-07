#ifndef COMPILER_ADDPROPAGATION_H
#define COMPILER_ADDPROPAGATION_H

#include "compiler/common.h"
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AddPropagationEntry {
    struct PCodeInstruction *instruction;
    struct CodeMotionEntryLink *list;
};
#pragma pack(push, 2)
struct CodeBits {
    UInt32 *gen;
    UInt32 *kill;
    UInt32 *in;
    UInt32 *out;
};
#pragma pack(pop)
extern void propagate_add_operands(SInt32 index);
extern void compute_add_propagation_in_out(void);
extern void build_add_propagation_entries(void);
extern void fn_00521950(void);
extern int can_propagate_add(int recordIndex, int useIndex);
extern void fn_00521480(void);
extern int *add_propagation_entry_counts;
extern SInt32 add_propagation_entry_count;
extern int *add_propagation_block_bit_indices;
extern struct CodeBits *addPropagationBlockBits;
extern struct AddPropagationEntry *add_propagation_entries;
#if VERSION >= VERSION_GC_2_7
extern void COpt_AddPropagation(Object *function);
#else
extern void COpt_AddPropagation(void);
#endif
extern SInt32 addPropagationChanged;
extern SInt32 gAddPropagationChanged;

#ifdef __cplusplus
}
#endif

#endif
