#ifndef COMPILER_SWITCH_H
#define COMPILER_SWITCH_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A run of switch case values and the label they branch to (case_ranges; Switch). */
struct CaseRange {
    CInt64 base;
    CInt64 width;
    struct PCodeLabel *info;
};
#pragma options align = mac68k
struct Scratch {
    int value;
};
#pragma options align = reset
#pragma options align = mac68k
struct SwitchCase {
    struct SwitchCase *next; /* 0x00: reconstruct_switch_info links case nodes */
    struct CLabel *label;    /* 0x04: Switch_GenerateSwitch emits case labels */
    CInt64 min;              /* 0x08: reconstruct_switch_info copies caseValue */
};
#pragma options align = reset
extern void Switch_GenerateSwitch(ENode *expression, struct SwitchInfo *cases);
extern void generate_switchtable_dispatch(ENode *node, struct SwitchInfo *cases);
extern Object *create_switchtable(void);
extern void emit_case_ranges(ENode *expr);
extern void emit_case_range_binary_tree(unsigned int firstCase, int lastCase);
extern void emit_case_range_binary_search(int a, int b);
extern int compare_switch_case_min(const void *a, const void *b);
extern void build_case_ranges(Type *type, SwitchCase *list, CLabel *defaultCase);
extern CInt64 data_005608f8;
extern struct CaseRange *case_ranges;
extern SInt32 switch_case_count;
extern SInt32 case_range_count;
extern CInt64 data_00581160;
extern CInt64 data_00581168;
extern CInt64 switchtable_base;
extern SInt16 data_00581178;
extern SInt16 switchRegHi;
extern struct Type *switch_expr_type;
extern struct PCodeLabel *default_case_label;
extern CInt64 switchtable_max;
extern UInt32 data_00581188;
extern struct SwitchCase **data_00581150;

#ifdef __cplusplus
}
#endif

#endif
