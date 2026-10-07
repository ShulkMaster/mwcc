#ifndef COMPILER_REGISTERINFO_H
#define COMPILER_REGISTERINFO_H

#include "compiler/common.h"
#include "compiler/Registers.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VERSION >= VERSION_GC_2_7
extern short last_exception_register[RegClassMax];
extern short first_fe_temporary_register[RegClassMax];
extern short _FP_;
extern short _CALLER_SP_;
extern char *special_register_names[RegClassMax][RegisterMax];
extern short high_offset;
extern short low_offset;
extern short high_reg;
extern short low_reg;
extern short high_reg2;
extern short low_reg2;

extern void asm_used_register(enum RegisterClass rclass, short reg);
extern void retain_register(Object *obj, enum RegisterClass rclass, short reg);
extern void retain_GPR_pair(Object *obj, short reg, short regHi);
extern int is_register_object(Object *obj);
extern int GetABIFirstNonVolatile(enum RegisterClass rclass);
extern char GetRegisterClassName(enum RegisterClass rclass);
extern void setup_diagnostic_reg_strings(void);
extern void init_target_registers(void);
extern void fn_004d0750(void);
extern void fn_004d0790(void);
extern void assign_register_by_type(Object *obj);
extern void assign_GPR_pair(Object *obj);
extern void open_fe_temp_registers(void);
extern void set_last_exception_registers(void);
extern void save_before_coloring_nonvolatile_registers(enum RegisterClass rclass);
extern void reset_nonvolatile_registers(enum RegisterClass rclass);
extern void init_endian(void);
extern void update_asm_nonvolatile_registers(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
