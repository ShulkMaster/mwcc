#ifndef COMPILER_MACHINESIMULATION603_H
#define COMPILER_MACHINESIMULATION603_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One opcode's entry in a machine model's table: its class there, and its length. */
struct MachineOpcodeInfo {
    UInt8 executionUnit;
    SInt8 latency;
    UInt8 initialStageCycles;
    UInt8 secondStageCycles;
    UInt8 thirdStageCycles;
    UInt8
        fourthStageCycles; /* 0x05: MachineSimulation601.c advance_instruction_pipeline loads sclass_pipeline_table[opcode * 6 + 3] for the fourth stage countdown. */
};
extern int get_opcode_table_entry(PCodeInstruction *instruction);
extern void retire_and_advance_pending_instructions(void);
extern void assign_entry_to_execution_unit(short *entry);
extern int fn_0052dfa0(PCodeInstruction *instruction);
extern void fn_0052e000(void);
extern char opcode_table[];
extern SInt32 pending_instruction_countdown[];
extern unsigned int DAT_00582cf4[];
extern struct {
    short *entry;
    int value;
} data_00582ca0[8];
extern Object *data_00582ca8;
extern SInt32 data_00582cac;
extern SInt32 pending_instruction;
extern SInt32 data_00582cb4;
extern PCodeInstruction *data_00582cb8;
extern SInt32 data_00582cbc;
extern PCodeInstruction *data_00582cc0;
extern SInt32 data_00582cc4;
extern PCodeInstruction *data_00582cc8;
extern SInt32 data_00582ccc;
extern PCodeInstruction *data_00582cd0;
extern SInt32 data_00582cd4;
extern Object *data_00582cd8;
extern SInt32 data_00582cdc;
extern int data_00582ce0;
extern SInt32 pending_instruction_count;
extern UInt32 pending_instruction_retire_index;
extern unsigned int execution_unit_entry_index;
extern struct {
    short *entry;
    int value;
} data_00582cf0[5];
extern SInt32 data_00582cf8;
extern SInt32 data_00582d00;
extern SInt32 data_00582d08;
extern SInt32 data_00582d10;
extern SInt8 opcode_simulation_table[];
extern SInt32 get_adjusted_latency(struct PCodeInstruction *p);
extern MachineOpcodeInfo machine_opcode_info[];

#ifdef __cplusplus
}
#endif

#endif
