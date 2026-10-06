#ifndef COMPILER_MACHINESIMULATION604_H
#define COMPILER_MACHINESIMULATION604_H

#include "compiler/common.h"
#include "compiler/MachineSimulation603.h"

#ifdef __cplusplus
extern "C" {
#endif

struct InstructionValueEntry {
    struct PCodeInstruction *instruction;
    int value;
};
struct SClassInfo {
    SInt8 initialStageCycles; /* 0x00: assign_instruction_to_execution_unit initializes the execution-unit countdown. */
    SInt8 secondStageCycles;  /* 0x01: advance_instruction_stages_and_retire initializes the second-stage countdown. */
    SInt8 thirdStageCycles;   /* 0x02: advance_instruction_stages_and_retire initializes the third-stage countdown. */
    UInt8 unusedBytes
        [3]; /* 0x03: MachineSimulation604.c opcode_sclass_info accesses only the first three bytes of each six-byte entry. */
};
extern int get_opcode_table_value(PCodeInstruction *instruction);
extern void advance_instruction_stages_and_retire(void);
extern void assign_instruction_to_execution_unit(struct PCodeInstruction *instruction);
extern void fn_0052eb60(void);
extern SInt32 get_size_rec_latency(struct PCodeInstruction *p);
extern int can_issue_instruction(struct PCodeInstruction *instruction);
extern char opcode_table_values[];
extern SInt32 DAT_00582de4;
extern unsigned int next_instruction_slot;
extern PCodeInstruction *data_00582d98;
extern PCodeInstruction *data_00582da0;
extern FuncArg *data_00582da8;
extern SInt32 data_00582db0;
extern PCodeInstruction *data_00582db8;
extern SInt32 data_00582dc0;
extern PCodeInstruction *data_00582dc8;
extern SInt32 data_00582dd0;
extern PCodeInstruction *data_00582dd8;
extern PCodeInstruction *data_00582ddc;
extern int data_00582de0;
extern SInt32 instruction_retire_index;
extern InstructionValueEntry execution_unit_instructions[9];
extern InstructionValueEntry instruction_ring[16];
extern InstructionValueEntry execution_unit_instructions[9];
extern MachineOpcodeInfo machineOpcodeInfo604[];
extern SClassInfo opcode_sclass_info[];

#ifdef __cplusplus
}
#endif

#endif
