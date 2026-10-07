#ifndef COMPILER_COLORING_H
#define COMPILER_COLORING_H

#include <stddef.h>
#include "compiler/common.h"
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VERSION >= VERSION_GC_2_7
enum RegisterClass {
    RegClass_Invalid = -1,
    RegClass_SPR = 0,
    RegClass_CRFIELD = 1,
    RegClass_VR = 2,
    RegClass_FPR = 3,
    RegClass_GPR = 4,
    RegClassMax = 5
};
extern enum RegisterClass coloring_class;
#else
enum RegisterClass { RegClass_GPR = 0, RegClass_FPR = 1, RegClass_VR = 9 };
#endif
enum InterferenceFlags {
    Interference_Spilled = 0x01,
    Interference_Simplified = 0x02,
    Interference_Coalesced = 0x04,
    Interference_CoalesceTarget = 0x08,
    Interference_SecondOfPair = 0x10,
    Interference_FirstOfPair = 0x20
};
#pragma pack(push, 2)
struct InterferenceNode {
    struct InterferenceNode *next;
    struct Object *object;
#if VERSION >= VERSION_GC_2_7
    struct PCodeInstruction *definition;
#endif
    int spill_cost;
    short virtual_register;
    short degree;
    short physical_register;
#if VERSION >= VERSION_GC_2_7
    unsigned short flags;
#else
    unsigned char flags;
    unsigned char unknown_13;
#endif
    short neighbor_count;
    short neighbors[1];
};
#pragma pack(pop)
extern void Coloring_AllocateRegisters(Object *function);
extern void Coloring_CommitAssignments(int reg_class, int register_count);
extern int Coloring_SelectColors(int register_class, InterferenceNode *node);
extern InterferenceNode *Coloring_SimplifyGraph(int allocation, int register_count, int node_count);
#if VERSION >= VERSION_GC_2_7
extern void Coloring_MarkSpecialRegisters(char reg_class);
#else
extern void Coloring_SetupVRs(void);
extern void Coloring_SetupFPRs(void);
extern void Coloring_SetupGPRs(void);
#endif
extern float float_max;
extern float data_005630a0;
extern short gColoringRegisterCount;
extern struct InterferenceNode **gInterferenceGraph;

#ifdef __cplusplus
}
#endif

#endif
