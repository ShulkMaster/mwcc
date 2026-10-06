#ifndef COMPILER_MACHINESIMULATION821_H
#define COMPILER_MACHINESIMULATION821_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct IndexedRecord {
    unsigned char header[0x14];
    short index;
};
struct QueueSlot {
    struct PCodeInstruction *obj;
    SInt32 flag;
};
extern unsigned char DAT_00578e50[];
extern char DAT_00578e52[];
extern SInt8 data_00578e53[];
extern SInt32 data_00583018[];
extern unsigned char data_00583020[];
extern SInt32 data_00583028;
extern unsigned char data_00583030[];
extern struct PCodeInstruction *data_00583038[];
extern SInt32 data_00583040;
extern int data_00583048;
extern UInt32 data_0058304c;
extern UInt32 data_00583050;
extern unsigned int record_enqueue_index;
extern SInt32 data_00583060;
extern SInt32 data_00583068;
extern SInt32 data_00583070;
extern SInt32 data_00583078;
extern SInt32 data_00583080;
extern void fn_00530660(void);
extern void fn_00530830(IndexedRecord *record);
extern int fn_005308b0(struct PCodeInstruction *pcode);
extern void reset_spill_state(void);
extern int get_instruction_opcode_table_entry(PCodeInstruction *instruction);
extern char DAT_00578e55[];
extern int get_instruction_cost(PCodeInstruction *instruction);
extern char instruction_costs[];
extern QueueSlot queue_slots[6];

#ifdef __cplusplus
}
#endif

#endif
