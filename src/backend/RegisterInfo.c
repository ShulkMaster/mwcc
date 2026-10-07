#define CERROR_FILE "RegisterInfo.c"
#include "compiler/RegisterInfo.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/CodeGen.h"
#include "compiler/CPrep.h"
#include "compiler/PCode.h"
#include "compiler/PPCError.h"
#include "compiler/objects.h"
#include "compiler/types.h"
#include "version.h"
#include <string.h>

#if VERSION >= VERSION_GC_2_7
enum { STRUCT_VECTOR_FIRST = 4, STRUCT_VECTOR_LAST = 14 };
short last_exception_register[RegClassMax];
short first_fe_temporary_register[RegClassMax];
short _FP_;
short _CALLER_SP_;
char *special_register_names[RegClassMax][RegisterMax];
short used_regs_before_coloring;
UInt8 save_state[RegisterMax];

void update_asm_nonvolatile_registers(void)
{
    enum RegisterClass rclass;
    int i;
    int reg;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        reg = n_nonvolatile_registers[rclass];
        for (i = n_nonvolatile_registers[rclass] - 1; i >= 0; i--) {
            if (reg_state[rclass][nonvolatile_registers[rclass][i]] == RegState_Used)
                break;
            reg--;
        }
        if (reg > used_nonvolatile_registers[rclass])
            used_nonvolatile_registers[rclass] = reg;
    }
}

void init_endian(void)
{
    if (copts.nativeByteOrder) {
        high_offset = 4;
        low_offset = 0;
        high_reg = 4;
        low_reg = 3;
        high_reg2 = 6;
        low_reg2 = 5;
    } else {
        high_offset = 0;
        low_offset = 4;
        high_reg = 3;
        low_reg = 4;
        high_reg2 = 5;
        low_reg2 = 6;
    }
}

void reset_nonvolatile_registers(enum RegisterClass rclass)
{
    used_nonvolatile_registers[rclass] = used_regs_before_coloring;
    memcpy(reg_state[rclass], save_state, sizeof(save_state));
}

void save_before_coloring_nonvolatile_registers(enum RegisterClass rclass)
{
    used_regs_before_coloring = used_nonvolatile_registers[rclass];
    memcpy(save_state, reg_state[rclass], sizeof(save_state));
}

static VarInfo *Registers_GetNewVarInfo(void)
{
    VarInfo *vi = galloc(sizeof(VarInfoGC27));
    memclrw(vi, sizeof(VarInfoGC27));
    return vi;
}

VarInfoGC27 *Registers_GetInfoGC27(Object *object)
{
    switch (object->datatype) {
        case DDATA:
            if (!object->u.data.info)
                object->u.data.info = Registers_GetNewVarInfo();
            return (VarInfoGC27 *)object->u.data.info;
        case DLOCAL:
            if (!object->u.var.info)
                CError_FATAL(647);
            return (VarInfoGC27 *)object->u.var.info;
        case DABSOLUTE:
            if (!object->u.data.info)
                object->u.data.info = Registers_GetNewVarInfo();
            return (VarInfoGC27 *)object->u.data.info;
        default:
            CError_FATAL(660);
            return NULL;
    }
}

void set_last_exception_registers(void)
{
    last_exception_register[RegClass_GPR] = used_virtual_registers[RegClass_GPR] - 1;
    last_exception_register[RegClass_FPR] = used_virtual_registers[RegClass_FPR] - 1;
    last_exception_register[RegClass_VR] = used_virtual_registers[RegClass_VR] - 1;
}

void open_fe_temp_registers(void)
{
    int r;

    r = used_virtual_registers[RegClass_GPR];
    first_fe_temporary_register[RegClass_GPR] = last_temporary_register[RegClass_GPR] = r;
    r = used_virtual_registers[RegClass_FPR];
    first_fe_temporary_register[RegClass_FPR] = last_temporary_register[RegClass_FPR] = r;
    r = used_virtual_registers[RegClass_VR];
    first_fe_temporary_register[RegClass_VR] = last_temporary_register[RegClass_VR] = r;
}

void asm_used_register(enum RegisterClass rclass, short reg)
{
    int i;

    if (reg >= n_real_registers[rclass])
        return;
    if (reg_state[rclass][reg] == RegState_Free) {
        if (reg == nonvolatile_registers[rclass][used_nonvolatile_registers[rclass]]) {
            if (assignable_registers[rclass] > 0)
                assignable_registers[rclass]--;
            reg_state[rclass][reg] = RegState_Used;
            used_nonvolatile_registers[rclass]++;
        } else {
            for (i = used_nonvolatile_registers[rclass]; i < n_nonvolatile_registers[rclass]; i++) {
                if (reg == nonvolatile_registers[rclass][i]) {
                    reg_state[rclass][reg] = RegState_Used;
                    if (assignable_registers[rclass] > 0)
                        assignable_registers[rclass]--;
                }
            }
        }
    }
}

void retain_register(Object *obj, enum RegisterClass rclass, short reg)
{
    VarInfoGC27 *vi;

    CError_ASSERT(95, reg < RegisterMax);

    if (reg_state[rclass][reg] == RegState_Free) {
        assignable_registers[rclass]--;
        reg_state[rclass][reg] = RegState_Used;
        if (reg == nonvolatile_registers[rclass][used_nonvolatile_registers[rclass]])
            used_nonvolatile_registers[rclass]++;
    }

    if (obj) {
        vi = Registers_GetInfoGC27(obj);
        vi->rclass = rclass;
        vi->flags |= VarInfoFlag_HasRegister;
        vi->reg = reg;
    }
}

void retain_GPR_pair(Object *obj, short reg, short regHi)
{
    VarInfoGC27 *vi;

    retain_register(NULL, RegClass_GPR, reg);
    retain_register(NULL, RegClass_GPR, regHi);

    if (obj) {
        vi = Registers_GetInfoGC27(obj);
        vi->rclass = RegClass_GPR;
        vi->flags |= VarInfoFlag_HasRegister | VarInfoFlag_RegisterPair;
        vi->reg = reg;
        vi->regHi = regHi;
    }
}

int GetABIFirstNonVolatile(enum RegisterClass rclass)
{
    switch (rclass) {
        case RegClass_SPR:
            return 3;
        case RegClass_CRFIELD:
            return 2;
        case RegClass_VR:
            return 20;
        case RegClass_GPR:
            return 14;
        case RegClass_FPR:
            return 14;
        default:
            return -1;
    }
}

char GetRegisterClassName(enum RegisterClass rclass)
{
    switch (rclass) {
        case RegClass_VR:
            return 'v';
        case RegClass_GPR:
            return 'r';
        case RegClass_FPR:
            return 'f';
        default:
            CError_FATAL(242);
            return '?';
    }
}

void setup_diagnostic_reg_strings(void)
{
    register_class_name[RegClass_SPR] = "SPR";
    register_class_format[RegClass_SPR] = "spr%ld";
    register_class_name[RegClass_CRFIELD] = "CRFIELD";
    register_class_format[RegClass_CRFIELD] = "cr%ld";
    register_class_name[RegClass_VR] = "VR";
    register_class_format[RegClass_VR] = "vr%ld";
    register_class_name[RegClass_FPR] = "FPR";
    register_class_format[RegClass_FPR] = "f%ld";
    register_class_name[RegClass_GPR] = "GPR";
    register_class_format[RegClass_GPR] = "r%ld";
}

void assign_GPR_pair(Object *obj)
{
    VarInfoGC27 *vi;
    short reg;
    short regHi;

    vi = Registers_GetInfoGC27(obj);
    if (optimizing) {
        reg = used_virtual_registers[RegClass_GPR]++;
        regHi = used_virtual_registers[RegClass_GPR]++;
    } else {
        CError_ASSERT(554, assignable_registers[RegClass_GPR] >= 2);
        reg = obtain_nonvolatile_register(RegClass_GPR);
        regHi = obtain_nonvolatile_register(RegClass_GPR);
        retain_GPR_pair(obj, reg, regHi);
    }

    vi->rclass = RegClass_GPR;
    if (reg > 0 && regHi > 0) {
        vi->flags |= VarInfoFlag_HasRegister | VarInfoFlag_RegisterPair;
        vi->reg = reg;
        vi->regHi = regHi;
    } else {
        CError_FATAL(567);
    }
}

void assign_register_by_type(Object *obj)
{
    VarInfoGC27 *vi;
    Type *ty;
    Boolean flag;

    ty = obj->type;
    vi = Registers_GetInfoGC27(obj);
    flag = 0;
    vi->rclass = RegClassMax;
    vi->reg = 0;
    vi->regHi = 0;

    if (ty->type == TYPEINT || ty->type == TYPEENUM ||
        ((ty->type == TYPEPOINTER || ty->type == TYPEARRAY) && ty->type != TYPEARRAY) ||
        (ty->type == TYPEMEMBERPOINTER && ty->size == 4) ||
        (copts.operandsDebug && ty->type == TYPEFLOAT &&
         !(ty->type == TYPEFLOAT && TYPE_INTEGRAL(ty)->integral == 0x13))) {
        if (((ty->type == TYPEINT || ty->type == TYPEENUM) && ty->size == 8) ||
            (copts.operandsDebug && ty->type == TYPEFLOAT &&
             !(ty->type == TYPEFLOAT && TYPE_INTEGRAL(ty)->integral == 0x13) && ty->size != 4))
            flag = 1;
        vi->rclass = RegClass_GPR;
    } else if ((ty->type == TYPEFLOAT && !(ty->type == TYPEFLOAT && TYPE_INTEGRAL(ty)->integral == 0x13)) ||
               (ty->type == TYPEFLOAT && TYPE_INTEGRAL(ty)->integral == 0x13)) {
        vi->rclass = RegClass_FPR;
    } else if (ty->type == TYPESTRUCT && TYPE_STRUCT(ty)->stype >= STRUCT_VECTOR_FIRST &&
               TYPE_STRUCT(ty)->stype <= STRUCT_VECTOR_LAST) {
        vi->rclass = RegClass_VR;
    } else {
        return;
    }

    if (vi->rclass < RegClassMax) {
        if (flag) {
            CError_ASSERT(520, vi->rclass == RegClass_GPR);
            if (assignable_registers[vi->rclass] > 1)
                assign_GPR_pair(obj);
        } else {
            if (assignable_registers[vi->rclass] > 0)
                assign_register_to_variable(obj, vi->rclass);
        }
    }
}

static int first_nonvolatile_reg(enum RegisterClass rclass)
{
    int first;
    int i;
    int errors;
    VarInfoGC27 *vi;
    GlobalRegisterVariable *variable;
    int last;
    Object *obj;
    int used[32];
    char gap[2];
    char name[2];

    last = GetABIFirstNonVolatile(rclass);
    if (copts.ignore_global_reg_vars)
        return last;
    first = last;
    for (i = 0; i < 32; i++)
        used[i] = 0;
    for (variable = gGlobalRegisterVariables; variable; variable = variable->next) {
        if (rclass == variable->rclass) {
            used[variable->reg - first] = variable->reg;
            if (variable->reg >= last)
                last = variable->reg + 1;
            obj = variable->object;
            if (obj && obj->otype == OT_OBJECT) {
                CError_ASSERT(279, Registers_GetInfoGC27(obj)->reg == 0);
                if (reg_state[variable->rclass][variable->reg] == RegState_Free) {
                    reg_state[variable->rclass][variable->reg] = RegState_Used;
                    vi = Registers_GetInfoGC27(obj);
                    vi->flags |= VarInfoFlag_HasRegister;
                    vi->reg = variable->reg;
                    vi->regHi = 0;
                    vi->rclass = variable->rclass;
                } else {
                    name[0] = GetRegisterClassName(variable->rclass);
                    name[1] = 0;
                    PPCError_ReportError(202, obj, name, variable->reg);
                }
            } else {
                PPCError_ReportError(204, variable->name->name);
            }
        }
    }
    errors = 0;
    for (i = 0; i < gGlobalRegisterVariableLimit[rclass]; i++) {
        if (used[i] == 0) {
            for (i++; i < gGlobalRegisterVariableLimit[rclass]; i++) {
                if (used[i] != 0) {
                    gap[0] = GetRegisterClassName(rclass);
                    gap[1] = 0;
                    PPCError_ReportError(199, gap, used[i] - 1);
                    errors++;
                    break;
                }
            }
        }
    }
    if (errors)
        return first;
    return last;
}

void init_target_registers(void)
{
    int end;
    enum RegisterClass rclass;
    int reg;

    static int last_nonvolatile_reg[] = {3, 5, 31, 31, 31};
    static int nonvol_reserve[] = {0, 0, 0, 4, 3};

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        for (reg = 0; reg < RegisterMax; reg++)
            special_register_names[rclass][reg] = NULL;
    }

    special_register_names[RegClass_SPR][0] = "XER";
    special_register_names[RegClass_SPR][1] = "LR";
    special_register_names[RegClass_SPR][2] = "CTR";
    special_register_names[RegClass_SPR][3] = "VRSAVE";
    special_register_names[RegClass_GPR][1] = "SP";
    special_register_names[RegClass_GPR][13] = "SDA";

    setup_diagnostic_reg_strings();
    n_real_registers[RegClass_SPR] = 4;
    n_real_registers[RegClass_CRFIELD] = 8;
    n_real_registers[RegClass_VR] = 32;
    n_real_registers[RegClass_FPR] = 32;
    n_real_registers[RegClass_GPR] = 32;
    reg_state[RegClass_GPR][1] = RegState_Reserved;
    reg_state[RegClass_GPR][2] = RegState_Reserved;
    reg_state[RegClass_GPR][13] = RegState_Reserved;
    reg_state[RegClass_CRFIELD][5] = RegState_Reserved;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        n_nonvolatile_registers[rclass] = 0;
        if (last_nonvolatile_reg[rclass] >= 0) {
            end = first_nonvolatile_reg(rclass);
            for (reg = last_nonvolatile_reg[rclass]; reg >= end; reg--) {
                if (reg_state[rclass][reg] == RegState_Free) {
                    nonvolatile_registers[rclass][n_nonvolatile_registers[rclass]++] = reg;
                }
            }
        }

        assignable_registers[rclass] = n_nonvolatile_registers[rclass] - nonvol_reserve[rclass];
        if (assignable_registers[rclass] < 0)
            assignable_registers[rclass] = 0;

        n_scratch_registers[rclass] = 0;
        for (reg = 0; reg < n_real_registers[rclass]; reg++) {
            if (reg < GetABIFirstNonVolatile(rclass) || reg > last_nonvolatile_reg[rclass]) {
                if (reg_state[rclass][reg] == RegState_Free) {
                    scratch_registers[rclass][n_scratch_registers[rclass]++] = reg;
                }
            }
        }
    }

    _FP_ = -1;
    _CALLER_SP_ = -1;
    optimizing = copts.deleteDeadInstructions > 0 && !data_00588224;
}

void fn_004d0750(void)
{
    enum RegisterClass rclass;

    if (gGlobalRegisterVariablesAllowed) {
        memset(reg_state, 0, sizeof(reg_state));
        for (rclass = 0; rclass < RegClassMax; rclass++)
            first_nonvolatile_reg(rclass);
    }
}

void fn_004d0790(void)
{
    enum RegisterClass rclass;

    gGlobalRegisterVariables = NULL;
    gGlobalRegisterVariablesAllowed = 1;
    for (rclass = 0; rclass < RegClassMax; rclass++)
        gGlobalRegisterVariableLimit[rclass] = 29 - GetABIFirstNonVolatile(rclass);
}
#endif
