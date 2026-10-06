#ifndef COMPILER_MACHINESIMULATION601_H
#define COMPILER_MACHINESIMULATION601_H

#include "compiler/common.h"
#include "compiler/MachineSimulation603.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SInt32 get_latency(struct PCodeInstruction *p);
extern Boolean is_execution_unit_seven(int instruction);
extern void advance_instruction_pipeline(void);
extern void set_execution_unit_instruction(PCodeInstruction *instruction);
extern int is_execution_unit_available(PCodeInstruction *instruction);
extern void clear_instruction_and_globals(void);
extern signed char sclass_pipeline_table[];
extern struct InstructionCountdown {
    PCodeInstruction *instruction;
    unsigned int count;
} data_00582fe8[6];
extern SInt32 data_00582ff0;
extern SInt32 data_00582ff4;
extern SInt32 data_00582ff8;
extern SInt32 data_00582ffc;
extern FuncArg *data_00583000;
extern SInt32 data_00583004;
extern SInt32 data_00583008;
extern SInt32 data_0058300c;
extern SInt32 data_00583010;
extern SInt32 data_00583014;
extern MachineOpcodeInfo data_00578340[];

#ifdef __cplusplus
}
#endif

#endif
