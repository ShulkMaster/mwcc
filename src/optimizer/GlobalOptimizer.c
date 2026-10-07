#include "compiler/common.h"
#include "compiler/GlobalOptimizer.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/AddPropagation.h"
#include "compiler/CExpr2.h"
#include "compiler/CMangler.h"
#include "compiler/CPrec.h"
#include "compiler/CPrep.h"
#include "compiler/CodeGen.h"
#include "compiler/CodeMotion.h"
#include "compiler/ConstantPropagation.h"
#include "compiler/DWARF.h"
#include "compiler/LoadDeletion.h"
#include "compiler/LoopDetection.h"
#include "compiler/LoopOptimization.h"
#include "compiler/PCodeListing.h"
#include "compiler/Peephole.h"
#include "compiler/StrengthReduction.h"
#include "compiler/ValueNumbering.h"

static void COptimizer_DumpStage(Object *function, const char *stage)
{
    Object *obj = function;
    CodeGen_DumpPCode_004c4bd0(COptimizer_GetFunctionObject(obj)->name, stage);
}

static void COptimizer_DumpIfChanged(Object *function, int changed, const char *stage)
{
    if (changed && copts.cOptimizerDumpEnabled) {
        COptimizer_DumpStage(function, stage);
    }
}

static void COptimizer_ValueNumbering(Object *function, int mode)
{
#if VERSION >= VERSION_GC_2_7
    ValueNumbering_PerformValueNumbering(function, mode);
#else
    ValueNumbering_PerformValueNumbering(mode);
#endif
}

static void COptimizer_CopyPropagation(Object *function, int mode)
{
#if VERSION >= VERSION_GC_2_7
    COpt_CopyPropagation(function, mode);
#else
    COpt_CopyPropagation(mode);
#endif
}

static void COptimizer_AddPropagation(Object *function)
{
#if VERSION >= VERSION_GC_2_7
    COpt_AddPropagation(function);
#else
    COpt_AddPropagation();
#endif
}

static void COptimizer_LoadDeletion(Object *function)
{
#if VERSION >= VERSION_GC_2_7
    COpt_LoadDeletion(function);
#else
    COpt_LoadDeletion();
#endif
}

/* GC 2.7's propagation passes list the function themselves; earlier versions list it here. */
static void COptimizer_RunCopyPropagation(Object *function, int mode)
{
    COptimizer_CopyPropagation(function, mode);
#if VERSION < VERSION_GC_2_7
    COptimizer_DumpIfChanged(function, gCopyPropagationChanged, "AFTER COPY PROPAGATION");
#endif
}

static void COptimizer_RunAddPropagation(Object *function)
{
    COptimizer_AddPropagation(function);
#if VERSION < VERSION_GC_2_7
    COptimizer_DumpIfChanged(function, gAddPropagationChanged, "AFTER ADD PROPAGATION");
#endif
}

#if VERSION >= VERSION_GC_2_7
static void COptimizer_RunPeepholeForward(Object *function, const char *stage)
{
    if (copts.peepholeOptimizationEnabled) {
        if (copts.instructionSchedulingMode == 0 && copts.deleteDeadInstructions > 1) {
            Peephole_MergeAdjacentBlocks(function, 0);
        }
        Peephole_VisitBlocksWithMultipleInstructions(function);
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, stage);
        }
    }
}
#endif

void COptimizer_Optimize(Object *function)
{
    char level;

    if (copts.cOptimizerDumpEnabled) {
        COptimizer_DumpStage(function, "BEFORE GLOBAL OPTIMIZATION");
    }

    if ((level = copts.deleteDeadInstructions) == 2 || (gRunLevel2Pipeline && level > 2)) {
        COptimizer_ValueNumbering(function, 1);
        COptimizer_DumpIfChanged(function, gValueNumberingChanged, "AFTER CSE");
        COptimizer_RunCopyPropagation(function, 1);
        COptimizer_RunAddPropagation(function);
    } else if (level == 3) {
        COptimizer_Level3(function);
    } else if (level == 4) {
        COptimizer_Level4(function);
    }
}

void COptimizer_Level3(Object *function)
{
#if VERSION >= VERSION_GC_2_7
    COptimizer_RunPeepholeForward(function, "AFTER PEEPHOLE FORWARD");
#endif
    COptimizer_ValueNumbering(function, 0);
    COptimizer_DumpIfChanged(function, gValueNumberingChanged, "AFTER VALUE NUMBERING");
    COptimizer_RunCopyPropagation(function, 0);

    gCopyPropagationChanged = 0;
    COptimizer_RunAddPropagation(function);

    LoopDetection_DetectLoops();
    if (data_0058763c != NULL) {
        COpt_SetLoopCodeMotionMode(1);
        LoopDetection_ComputeLoopPropertiesRecursive();
        CodeMotion_VisitLoops();
#if VERSION >= VERSION_GC_2_7
        COptimizer_DumpIfChanged(function, gCodeMotionChanged, "AFTER CODE MOTION");
#endif
        COpt_SetLoopCodeMotionMode(0);
        LoopDetection_TraverseLoopsPostorder(data_0058763c);
#if VERSION < VERSION_GC_2_7
        COptimizer_DumpIfChanged(function, gCodeMotionChanged, "AFTER CODE MOTION");
#endif

        StrengthReduction_RunLoopPasses();
        if (gStrengthReductionChanged) {
#if VERSION >= VERSION_GC_2_7
            if (copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER STRENGTH REDUCTION");
            }
            COptimizer_CopyPropagation(function, 1);
#else
            COptimizer_CopyPropagation(function, 1);
            if (copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER STRENGTH REDUCTION");
            }
#endif
        }

        fn_005289b0();
        if (gLoopTransformChanged) {
#if VERSION >= VERSION_GC_2_7
            if (copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER LOOP TRANSFORMATIONS");
            }
            COptimizer_CopyPropagation(function, 1);
            COptimizer_AddPropagation(function);
#else
            COptimizer_CopyPropagation(function, 1);
            COptimizer_AddPropagation(function);
            if (copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER LOOP TRANSFORMATIONS");
            }
#endif
        }
    }

    if (!gCopyPropagationChanged) {
        COptimizer_CopyPropagation(function, 1);
        COptimizer_DumpIfChanged(function, gCopyPropagationChanged, "AFTER COPY PROPAGATION");
    }

    COpt_ConstantPropagation();
    if (gConstantPropagationChanged) {
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER CONSTANT PROPAGATION");
        }
        COptimizer_LoadDeletion(function);
#if VERSION < VERSION_GC_2_7
        COptimizer_DumpIfChanged(function, gLoadDeletionChanged, "AFTER LOAD DELETION");
#endif
        COptimizer_RunAddPropagation(function);
    }

#if VERSION >= VERSION_GC_2_7
    COptimizer_RunPeepholeForward(function, "AFTER PEEPHOLE FORWARD 2");
#endif
    COptimizer_ValueNumbering(function, 1);
    if (gValueNumberingChanged) {
#if VERSION >= VERSION_GC_2_7
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER VALUE NUMBERING 2");
        }
        COptimizer_CopyPropagation(function, 1);
#else
        COptimizer_CopyPropagation(function, 1);
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER VALUE NUMBERING 2");
        }
#endif
    }
}

void COptimizer_Level4(Object *function)
{
#if VERSION >= VERSION_GC_2_7
    COptimizer_RunPeepholeForward(function, "AFTER PEEPHOLE FORWARD");
#endif
    COptimizer_ValueNumbering(function, 0);
    COptimizer_DumpIfChanged(function, gValueNumberingChanged, "AFTER VALUE NUMBERING");
    COptimizer_RunCopyPropagation(function, 0);

    gCopyPropagationChanged = 0;
    COptimizer_RunAddPropagation(function);

    LoopDetection_DetectLoops();
    if (data_0058763c != NULL) {
        COpt_SetLoopCodeMotionMode(1);
        LoopDetection_ComputeLoopPropertiesRecursive();
        CodeMotion_VisitLoops();
#if VERSION >= VERSION_GC_2_7
        COptimizer_DumpIfChanged(function, gCodeMotionChanged, "AFTER CODE MOTION");
#endif
        COpt_SetLoopCodeMotionMode(0);
        LoopDetection_TraverseLoopsPostorder(data_0058763c);
#if VERSION < VERSION_GC_2_7
        COptimizer_DumpIfChanged(function, gCodeMotionChanged, "AFTER CODE MOTION");
#endif

        StrengthReduction_RunLoopPasses();
        if (gStrengthReductionChanged) {
#if VERSION >= VERSION_GC_2_7
            if (copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER STRENGTH REDUCTION");
            }
            COptimizer_CopyPropagation(function, 1);
            gCopyPropagationChanged = 0;
#else
            COptimizer_CopyPropagation(function, 1);
            if (copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER STRENGTH REDUCTION");
            }
#endif
        }

        fn_005289b0();
        if (gLoopTransformChanged) {
#if VERSION >= VERSION_GC_2_7
            if (copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER LOOP TRANSFORMATIONS");
            }
            COptimizer_CopyPropagation(function, 1);
            COptimizer_AddPropagation(function);
#else
            COptimizer_CopyPropagation(function, 1);
            COptimizer_AddPropagation(function);
            if (copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER LOOP TRANSFORMATIONS");
            }
#endif
        }
    }

    if (!gCopyPropagationChanged) {
        COptimizer_RunCopyPropagation(function, 1);
    }

    COpt_ConstantPropagation();
    if (gConstantPropagationChanged) {
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER CONSTANT PROPAGATION");
        }
        COptimizer_LoadDeletion(function);
#if VERSION < VERSION_GC_2_7
        COptimizer_DumpIfChanged(function, gLoadDeletionChanged, "AFTER LOAD DELETATION");
#endif

        if (gConstantPropagationChanged) {
            COptimizer_CopyPropagation(function, 1);
        }
        COptimizer_RunAddPropagation(function);

        if (gArrayToRegisterEnabled) {
            COpt_ArrayToRegister();
            if (gArrayToRegisterChanged && copts.cOptimizerDumpEnabled) {
                COptimizer_DumpStage(function, "AFTER ARRAY => REGISTER TRANSFORM");
                COpt_ConstantPropagation();
#if VERSION >= VERSION_GC_2_7
                if (copts.cOptimizerDumpEnabled) {
                    COptimizer_DumpStage(function, "AFTER CONSTANT PROPAGATION 2");
                }
                if (gConstantPropagationChanged) {
                    COptimizer_CopyPropagation(function, 1);
                }
#else
                if (gConstantPropagationChanged) {
                    COptimizer_CopyPropagation(function, 1);
                }
                if (copts.cOptimizerDumpEnabled) {
                    COptimizer_DumpStage(function, "AFTER CONSTANT PROPAGATION 2");
                }
#endif
            }
        }
    }

#if VERSION >= VERSION_GC_2_7
    COptimizer_RunPeepholeForward(function, "AFTER PEEPHOLE FORWARD 2");
#endif
    COptimizer_ValueNumbering(function, 1);
    if (gValueNumberingChanged) {
#if VERSION >= VERSION_GC_2_7
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER VALUE NUMBERING 2");
        }
        COptimizer_CopyPropagation(function, 1);
#else
        COptimizer_CopyPropagation(function, 1);
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER VALUE NUMBERING 2");
        }
#endif
    }

    if (gVectorArrayConversion && fn_0052ce10()) {
#if VERSION >= VERSION_GC_2_7
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER VECTOR ARRAY CONVERSION");
        }
        COptimizer_CopyPropagation(function, 0);
        COptimizer_CopyPropagation(function, 1);
#else
        COptimizer_CopyPropagation(function, 0);
        COptimizer_CopyPropagation(function, 1);
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER VECTOR ARRAY CONVERSION");
        }
#endif
    }

    LoopDetection_DetectLoops();
    if (data_0058763c == NULL) {
        return;
    }

    COpt_SetLoopCodeMotionMode(1);
    LoopDetection_ComputeLoopPropertiesRecursive();
    CodeMotion_VisitLoops();
    COptimizer_DumpIfChanged(function, gCodeMotionChanged, "AFTER CODE MOTION 2");

    COptimizer_ValueNumbering(function, 1);
    if (gValueNumberingChanged) {
#if VERSION >= VERSION_GC_2_7
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER VALUE NUMBERING 3");
        }
        COptimizer_CopyPropagation(function, 1);
#else
        COptimizer_CopyPropagation(function, 1);
        if (copts.cOptimizerDumpEnabled) {
            COptimizer_DumpStage(function, "AFTER VALUE NUMBERING 3");
        }
#endif
    }
}
