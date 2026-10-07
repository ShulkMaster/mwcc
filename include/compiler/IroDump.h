#ifndef COMPILER_IRODUMP_H
#define COMPILER_IRODUMP_H

#include "compiler/common.h"
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void IroDump_DumpFunction(char *value, int enabled);
extern Object *IroDump_GetObjRef(IROLinear *linear);
extern void IroDump_Print(const char *message, ...);
extern void IroDump_DumpExpressions(void);
extern void dump_flowgraph(void);
extern void IroDump_PrintBitSet(char *prefix, BitVector *bitset);
#if VERSION >= VERSION_GC_1_3
extern Boolean INT_005882b8;
#else
extern int INT_005882b8;
#endif
extern struct _FILE *iro_dump_output;
extern Object *data_005875b8;
extern void dump_linear_node(IROLinear *node);
extern char *PTR_s_EPOSTINC_0055268c[];
extern SInt32 IroDump_IsPowerOfTwo(IROLinear *node, SInt32 *bit);
extern int fn_0044d520(IROLinear *node);

#ifdef __cplusplus
}
#endif

#endif
