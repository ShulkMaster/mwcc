#ifndef COMPILER_LIVEVARIABLES_H
#define COMPILER_LIVEVARIABLES_H

#include "compiler/common.h"
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma options align = mac68k
struct PCodeLiveness {
    UInt32 *use;
    UInt32 *def;
    UInt32 *livein;
    UInt32 *liveout;
};
#pragma options align = reset
extern struct PCodeLiveness *gPCodeBlockLiveness;
#if VERSION >= VERSION_GC_2_7
extern void SpillCode_InitializeLiveness(Object *func);
extern void SpillCode_SolveLiveness(void);
extern void SpillCode_BuildLocalLiveness(void);
#else
extern void SpillCode_InitializeLiveness(Object *func, SInt32 mode, UInt32 nbits);
extern void SpillCode_SolveLiveness(UInt32 nbits);
extern void SpillCode_BuildLocalLiveness(int reg_class);
#endif
extern SInt32 SpillCode_IsDeadInstruction(PCodeInstruction *rec, SInt16 reg, UInt32 *live);

#ifdef __cplusplus
}
#endif

#endif
