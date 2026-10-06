#ifndef COMPILER_CTEMPLATETOOLS_H
#define COMPILER_CTEMPLATETOOLS_H

#include "compiler/common.h"
#include "compiler/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma options align = mac68k
struct CTStateElem {
    CTStateElem *next; /* 0x00: CTemplateNew_ParseTemplateArguments links template arguments */
    TemplParamID
        pid; /* 0x04: CTemplateTools_GetArgumentType matches it with key->u.pid; CTemplateNew_ParseTemplateArguments copies a parameter slot's id */
    union {
        struct Type *type;        /* 0x08: CError.c reads when pid.type != 0 */
        struct ENode *expression; /* 0x08: CError.c reads when pid.type == 0 */
    } argument;
    SInt32 qualifiers; /* 0x0c: parse_template_arguments stores parse.qual */
    Boolean bound;     /* 0x10: match_template_arguments marks matched slots */
    UInt8 unk11;       /* 0x11: CTemplateTools_CopySlotsToList copies this storage; no named accesses */
};
#pragma options align = reset
struct TemplateArgument {
    union {
        struct Type *type;
        struct ENode *expression;
    } argument;
    SInt32 qualifiers;
};

#pragma pack(push, 1)
struct TemplateLookupContext {
    struct TypeClassTemplate
        *owner; /* 0x00: fn_00516b50 matches record->enclosingTemplate against the original owner chain */
    struct TypeClassTemplate *currentClass;   /* 0x04: fn_00516b50 searches the corresponding current class namespace */
    void *unk8;                               /* 0x08: no named accesses in CTemplateTools.c; meaning unknown */
    struct TemplateParameterIDEntry *entries; /* 0x0c: no named accesses in CTemplateTools.c */
};
#pragma pack(pop)
struct TemplateParameterIDEntry {
    struct TemplateParameterIDEntry *next;
    struct {
        short index;
        char nindex;
        char type;
    } pid;
};
#pragma options align = mac68k
struct TemplateSlot {
    TemplateSlot *next;
    void *type1;
    TemplateArgument types;
    UInt8 flag;
    UInt8 pad11;
};
#pragma options align = reset
extern Type *make_bitfield_type(TemplateContext *ctx, Type *ty, ENode *node, UInt32 *out);
extern Boolean CTemplTool_TemplDepTypeCompare(TypeTemplDep *a, TypeTemplDep *b);
extern Boolean CTemplateTools_00517a40(ENode *n1, ENode *n2);
extern void CTemplTool_RemoveOuterTemplateArgumentNameSpace(NameSpace *ns);
extern void CTemplTool_MergeArgNames(Type *sourceFunc, Type *destinationFunc);
extern void CTemplTool_InsertTemplateParameter(NameSpace *scope, TemplateParameterRecord *source);
extern Type *resolve_templ_dep_pointer_target(TemplateContext *context, Type *typeArg, UInt32 *qualifiers);
extern TypeClassExt800 *fn_00517270(TypeClass *current, TypeClassExt800 *limit, TypeClassExt800 *target);
extern void CTemplTool_CheckTemplArgType(Type *type);
extern ENode *CTempl_MakeTemplDepExpr(ENode *left, UInt8 kind, ENode *right);
extern Boolean CTemplTool_IsTypeDepExpr(ENode *node);
extern void CTemplTool_RemoveTemplateArgumentNameSpace(NameSpace *args, TypeClassExt800 *func, CScopeSave *scope);
extern void CTemplTool_SetupOuterTemplateArgumentNameSpace(NameSpace *nameSpace);
extern struct TemplateFunction *CTemplTool_GetFuncTempl(Object *obj);
extern Type *resolve_templ_dep_type(TemplateContext *ctx, TypeTemplDep *arg, UInt32 *out);
extern struct CTStateElem *find_template_argument(struct TemplateContext *context, struct TemplParamID pid);
extern void CTemplateTools_00516930(void *context, struct TypeClassTemplate *function, CTStateElem *arguments);
extern ExceptSpecList *copy_resolved_except_spec_list(void *ctx, ExceptSpecList *n);
extern unsigned char CTemplateTools_IsDependentType(Type *type);
extern TypeClassExt800 *find_corresponding_instance_class(TemplateContext *list, TypeClass *targetClass);
extern UInt8 CTemplTool_IsIdenticalTemplArgList(CTStateElem *pattern, TemplateParameterRecord *argument);
extern UInt8 CTemplTool_IsSameTemplate(TemplateParameterRecord *parameter, CTStateElem *argument);
extern CTStateElem *CTemplateTools_CopySlotsToList(struct TemplateMatchState *src);
extern NameSpace *CTemplTool_SetupTemplateArgumentNameSpace(FuncArg *arglist, CTStateElem *targlist, Boolean flag);
extern TypeClass *CTemplateTools_GetTemplClass(TypeTemplDep *record);
extern NameSpace *CTemplateTools_InsertTemplateArgs(FuncArg *context, TypeClassExt800 *function, CScopeSave *scope);
extern FuncArg *CTemplateTools_005160b0(TemplateContext *ctx, FuncArg *args);
extern Type *CTemplateTools_GetArgumentType(CTStateElem *record, TypeTemplDep *key, unsigned int qualifiers,
                                            unsigned int *resultQualifiers);
extern void CTemplTool_MergeDefaultArgs(TemplateParameterRecord *destination, TemplateParameterRecord *source);
extern ENode *CTemplTool_DeduceExpr(TemplateContext *ctx, ENode *node);
extern Type *CTemplTool_IsDependentTemplate(struct TypeClassTemplate *templateClass, CTStateElem *arguments);
extern struct TypeClassTemplate *CTemplTool_IsTemplate(TypeTemplDep *reference);
extern CTStateElem *CTemplateTools_CopyCTStateElemList(CTStateElem *p);
extern struct ObjectReferenceEntry *CTemplateTools_PopObjectReferenceEntry(struct ObjectReferenceEntry *entry);
extern UInt8 CTemplTool_EqualParams(TemplateParameterRecord *left, TemplateParameterRecord *right, char copyValue);
extern UInt8 CTemplTool_EqualArgs(CTStateElem *left, CTStateElem *right);
extern Boolean CTemplTool_InitDeduceInfo(TemplateMatchState *info, TemplateParameterRecord *params, CTStateElem *args,
                                         Boolean flag);
extern Type *CTemplateTools_ResolveType(TemplateContext *ctx, Type *type, UInt32 *qual);
extern struct TypeClassTemplate *fn_00516b50(TemplateLookupContext *context, struct TypeClassTemplate *record);
extern Boolean CTemplateTools_MatchTypeAndCheckBoundSlots(Object *obj, Type *name, void *arg3);
extern Boolean CTemplateTools_IsTemplDepClassBase(TypeClass *type, TypeTemplDep *templateType);
extern Type *CTemplTool_ResolveMemberSelfRefs(TypeClass *tclass, Type *type, UInt32 *qualifiers);
extern SInt16 objectReferenceEntryCount;
struct CTStateElem;

#ifdef __cplusplus
}
#endif

#endif
