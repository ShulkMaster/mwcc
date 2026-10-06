#ifndef COMPILER_GLOBALOPTIMIZER_H
#define COMPILER_GLOBALOPTIMIZER_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void COptimizer_Optimize(Object *function);
extern void COptimizer_Level3(Object *function);
extern void COptimizer_Level4(Object *function);
extern struct Loop *data_0058763c;
extern int gCodeMotionChanged;
extern int gCopyPropagationChanged;
extern SInt32 gStrengthReductionChanged;
extern SInt32 gValueNumberingChanged;

#ifdef __cplusplus
}
#endif

#endif
