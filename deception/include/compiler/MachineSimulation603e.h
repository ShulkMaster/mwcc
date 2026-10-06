#ifndef COMPILER_MACHINESIMULATION603E_H
#define COMPILER_MACHINESIMULATION603E_H

#include "compiler/common.h"
#include "compiler/MachineSimulation603.h"

#ifdef __cplusplus
extern "C" {
#endif

struct InstructionCompletionEntry {
    struct PCodeInstruction *instruction;
    int completed;
};
struct InstructionTimingSlot {
    struct PCodeInstruction *instruction;
    int cycles;
};
extern void fn_0052e110(void);
extern int DAT_00582d5c;
extern signed char opcode_cycles[][6];
extern signed char data_005758a4[][6];
extern int data_00582d24;
extern struct PCodeInstruction *data_00582d28;
extern int data_00582d2c;
extern struct PCodeInstruction *data_00582d30;
extern int data_00582d34;
extern struct PCodeInstruction *data_00582d38;
extern int data_00582d3c;
extern struct PCodeInstruction *queuedInstruction;
extern int data_00582d44;
extern struct PCodeInstruction *data_00582d48;
extern int data_00582d4c;
extern struct PCodeInstruction *data_00582d50;
extern int data_00582d54;
extern unsigned int data_00582d60;
extern int get_instruction_opcode_table_value(PCodeInstruction *instruction);
extern void fn_0052e590(void);
extern int is_instruction_issuable(PCodeInstruction *instr);
extern void fn_0052e450(struct PCodeInstruction *instruction);
extern signed char DAT_005758a2[];
extern char instruction_opcode_table[];
extern int DAT_00582d58;
extern int opcodeValues[];
extern struct PCodeInstruction *data_00582d20;
extern unsigned int data_00582d64;
extern unsigned int data_00582d6c[];
extern SInt32 data_00582d70;
extern SInt32 data_00582d78;
extern SInt32 data_00582d80;
extern SInt32 data_00582d88;
extern SInt32 fn_0052e640(struct PCodeInstruction *p);
extern InstructionCompletionEntry instruction_completion_entries[5];
extern struct InstructionTimingSlot instruction_timing_slots[8];
extern MachineOpcodeInfo machineOpcodeInfo[];

#ifdef __cplusplus
}
#endif

#endif
