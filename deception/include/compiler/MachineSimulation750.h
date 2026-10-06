#ifndef COMPILER_MACHINESIMULATION750_H
#define COMPILER_MACHINESIMULATION750_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct OptPair {
    struct PCodeInstruction
        *instruction; /* 0x00: record_opt_arg stores p; advance_simulation_pipeline matches completed instructions */
    SInt32
        completed; /* 0x04: record_opt_arg clears; advance_simulation_pipeline sets on completion and tests before retirement */
};
struct PoolEntry {
    struct PCodeInstruction *obj;
    SInt32 cnt;
};
extern PoolEntry data_00582e70[9];
extern struct PoolEntry data_00582e70[9];
extern int get_opcode_table_first_entry(PCodeInstruction *instruction);
extern void advance_simulation_pipeline(void);
extern void record_instruction_kind(PCodeInstruction *p);
extern void reset_simulation_pipeline(void);
extern int fn_0052f330(PCodeInstruction *instruction);
extern int fn_0052f120(struct PCodeInstruction *instruction);
extern struct OpcodeOperandInfo {
    char count;
    UInt8 reserved[4];
    UInt8 kind;
} DAT_00576f29[];
extern char opcode_table_entries[];
extern struct PCodeInstruction *DAT_00582ea8;
extern SInt32 pending_opt_args;
extern UInt32 simulation_pipeline_index;
extern UInt32 opt_arg_index;
extern unsigned int DAT_00582ed8;
extern unsigned int DAT_00582ee0;
extern unsigned int DAT_00582ee8;
extern struct OpcodeScheduleInfo {
    UInt8 kind;
    UInt8 reserved[2];
    SInt8 latency3;
    SInt8 latency4;
    UInt8 reserved5;
} data_00576f28[];
extern struct PCodeInstruction *data_00582e78;
extern SInt32 data_00582e7c;
extern struct PCodeInstruction *data_00582e80;
extern SInt32 data_00582e84;
extern SInt32 data_00582e8c;
extern struct PCodeInstruction *data_00582e90;
extern SInt32 data_00582e94;
extern SInt32 data_00582e9c;
extern SInt32 data_00582ea4;
extern SInt32 data_00582eac;
extern SInt32 data_00582eb4;
extern struct PCodeInstruction *data_00582eb8;
extern struct PCodeInstruction *data_00582ebc;
extern int data_00582ec0;
extern struct PCodeInstruction *data_00582eb0;
extern struct PCodeInstruction *DAT_00582e98;
extern struct PCodeInstruction *DAT_00582ea0;
extern struct PCodeInstruction *DAT_00582e88;
extern unsigned int _DAT_00582ef0;
extern unsigned int _DAT_00582ef8;
extern OptPair DAT_00582ed0[6];

#ifdef __cplusplus
}
#endif

#endif
