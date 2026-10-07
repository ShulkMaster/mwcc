#ifndef COMPILER_REGISTERS_H
#define COMPILER_REGISTERS_H

#include "compiler/common.h"
#include "compiler/CPrep.h"
#include "version.h"
#if VERSION >= VERSION_GC_2_7
#include "compiler/Coloring.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma options align = mac68k
struct VarInfo {
    struct Object *func;
    SInt32 usage;
    struct BufferedToken deftoken;
    SInt16 varnumber;
    UInt8 noregister;
    UInt8 used;
    SInt16 reg;
    SInt16 regHi;
    UInt8 is_fpr;
    UInt8 in_param_area;
    UInt8 is_vector;
    UInt8
        alignmentPadding; /* 0x2b: Registers_GetInfo allocates and clears 44 bytes; unused trailing alignment byte after is_vector. */
};
#pragma options align = reset
#if VERSION >= VERSION_GC_2_7
enum VarInfoFlags { VarInfoFlag_HasRegister = 0x02, VarInfoFlag_RegisterPair = 0x04 };
#pragma options align = mac68k
struct VarInfoGC27 {
    struct Object *func;
    SInt32 usage;
    struct BufferedToken deftoken;
    SInt16 varnumber;
    UInt8 noregister;
    UInt8 used;
    UInt8 flags;
    UInt8 rclass;
    SInt16 reg;
    SInt16 regHi;
};
#pragma options align = reset
#pragma pack(push, 2)
struct GlobalRegisterVariable {
    struct GlobalRegisterVariable *next;
    struct Object *object;
    struct HashNameNode *name;
    SInt8 rclass;
    SInt8 reg;
    struct NameSpace *nspace;
    UInt8 is_static;
};
#pragma pack(pop)
extern VarInfoGC27 *Registers_GetInfoGC27(Object *object);
extern int n_real_registers[RegClassMax];
extern GlobalRegisterVariable *gGlobalRegisterVariables;
#define RegisterMax 32
#define RegState_Free 0
#define RegState_Used 1
#define RegState_Reserved 2
extern int used_virtual_registers[RegClassMax];
extern int used_nonvolatile_registers[RegClassMax];
extern int assignable_registers[RegClassMax];
extern int n_scratch_registers[RegClassMax];
extern int scratch_registers[RegClassMax][RegisterMax];
extern int n_nonvolatile_registers[RegClassMax];
extern int nonvolatile_registers[RegClassMax][RegisterMax];
extern UInt8 reg_state[RegClassMax][RegisterMax];
extern int first_temporary_register[RegClassMax];
extern int last_temporary_register[RegClassMax];
extern char *register_class_name[RegClassMax];
extern char *register_class_format[RegClassMax];
extern int coloring;
extern int gGlobalRegisterVariableLimit[RegClassMax];
extern UInt8 gGlobalRegisterVariablesAllowed;
extern int optimizing;
extern void assign_register_to_variable(Object *obj, enum RegisterClass rclass);
extern int available_registers(enum RegisterClass rclass);
extern UInt32 volatile_registers(enum RegisterClass rclass);
extern short obtain_nonvolatile_register(enum RegisterClass rclass);
extern void open_temp_registers(void);
extern void check_temp_registers(void);
extern void close_temp_registers(void);
#endif
extern void Registers_BindVR(Object *obj, short vr);
extern void Registers_BindFPR(Object *obj, SInt16 regnum);
extern void Registers_BindGPR(Object *obj, SInt16 reg);
extern short gAvailableSavedFPRs;
extern SInt16 gAvailableSavedVRs;
extern void Coloring_ResetVRColors(void);
extern void Coloring_ResetFPRColors(void);
extern void Coloring_ResetGPRColors(void);
extern void Registers_SetupVRs(void);
extern void Registers_SetupFPRs(void);
extern void Registers_SetupGPRs(void);
extern VarInfo *Registers_GetInfo(Object *object);
extern void Registers_CloseCoalesceWindow(void);
extern void Registers_UpdateCoalesceWindow(void);
extern void Registers_CheckpointCoalesceWindow(void);
extern void Registers_SnapshotInitialObjectRange(void);
extern void Registers_BeginCoalesceWindow(void);
extern short Coloring_ClaimVRColor(void);
extern short Coloring_ClaimFPRColor(void);
extern short Coloring_ClaimGPRColor(void);
extern unsigned int Coloring_VRColorMask(void);
extern unsigned int Coloring_FPRColorMask(void);
extern unsigned int Coloring_GPRColorMask(void);
extern int Registers_AvailableVRs(void);
extern int Registers_AvailableFPRs(void);
extern int Registers_AvailableGPRs(void);
extern void Registers_BindVR(Object *obj, short vr);
extern void Registers_BindFPR(Object *obj, SInt16 regnum);
extern void Registers_BindGPRPair(Object *obj, SInt16 reg0, SInt16 reg1);
extern void Registers_AllocateVR(Object *obj);
extern void Registers_AllocateFPR(Object *obj);
extern void Registers_AllocateGPRPair(Object *obj);
extern void Registers_AllocateGPR(Object *obj);
extern unsigned char Registers_AreNonvolatileVRUsed(void);
extern void Registers_BindGPR(Object *obj, SInt16 reg);
extern void Registers_SetupStackBaseReg(void);
extern SInt32 fn_004c15f0(void);
extern short gSaveSpan;
extern unsigned char gSavedUsedPhysicalRegisters[32];
extern short gFPRCoalesceLast;
extern short gFPRCounterCheckpoint;
extern short gGPRCoalesceLast;
extern short gGPRCounterCheckpoint;
extern short gInitialObjectFPRLast;
extern short gInitialObjectVRLast;
extern SInt32 gUseVirtualRegisterNumbers_00587f00;
extern unsigned char gUsedPhysicalFPR[32];
extern UInt8 gUsedPhysicalGPR[];
extern UInt8 gUsedPhysicalVR[];
extern short gVRCoalesceLast;
extern short gVRCounterCheckpoint;
extern void Registers_InitRegisterState(void);
extern UInt32 Registers_GetOperandRegMask(PCodeBlock *list);
extern void Registers_InvalidateCSE(COptCSE *node);
extern int Registers_GetCSEWeight(COptCSE *tree);
extern Boolean Registers_ContainsCOptCSE(COptCSE *target, COptCSE *node);
extern void Registers_DivideUses(COptCSE *node, SInt16 divisor);

#ifdef __cplusplus
}
#endif

#endif
