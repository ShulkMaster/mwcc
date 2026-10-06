#ifndef COMPILER_CMANGLER_H
#define COMPILER_CMANGLER_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern HashNameNode *COptimizer_GetFunctionObject(Object *obj);
extern HashNameNode *CMangler_GetLinkName(Object *obj);
extern void mangle_function_name(HashNameNode *name, NameSpace *chain, Type *func);
extern void mangle_args(FuncArg *args);
extern void fn_004c2ac0(Type *type, SInt32 flag);
extern HashNameNode *CMangler_ConversionFuncName(Type *type, UInt32 qual);
extern HashNameNode *get_object_link_name(Object *object);
extern HashNameNode *CMangler_GetCovariantFunctionName(Object *object, Type *type);
extern void mangle_type(Type *type, UInt32 flags);
extern void mangle_qualified_name(NameSpace *nameSpace, const char *name);
extern HashNameNode *CMangler_TemplateInstanceName(HashNameNode *name, CTStateElem *list);
extern HashNameNode *CMangler_ThunkName(Object *input, int offset, int adjustment, int index);
extern HashNameNode *CMangler_RTTIObjectName(Type *type, unsigned int flags);
extern HashNameNode *CMangler_VTableName(TypeClass *entry);
extern unsigned char data_00561a60[];
extern unsigned char covariant_function_name_prefix[];
extern GList data_00583548;
extern HashNameNode *CMangler_OperatorName(short token);
extern char *CMangler_GetOperator(HashNameNode *name);
extern struct HashNameNode *assignment_operator_name;
extern char operator_new_name[];
extern char delete_operator_name[];
extern char plus_operator_code[];
extern char minus_operator_name[];
extern char operator_code_ml[];
extern char operator_dv_name[];
extern char md_operator_code[];
extern char operator_er_code[];
extern char data_00561cc0[];
extern char operator_or_code[];
extern char co_operator_code[];
extern char operator_nt_code[];
extern char lt_operator_name[];
extern char gt_operator_name[];
extern char operator_ls[];
extern char operator_rs_code[];
extern char eq_operator_name[];
extern char operator_ne_code[];
extern char operator_le_name[];
extern char ge_operator_code[];
extern char data_00561d70[];
extern char operator_name_code[];
extern char operator_names[];
extern char operator_name_mm[];
extern char data_00561d90[];
extern char operator_rm_code[];
extern char operator_code_rf[];
extern char operator_call_code[];
extern char operator_vc_name[];
extern void CMangler_Setup(void);

#ifdef __cplusplus
}
#endif

#endif
