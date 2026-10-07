#include "compiler/common.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/types.h"
#include "compiler/BE_symbol.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CExpr2.h"
#include "compiler/CFunc.h"
#include "compiler/CInline.h"
#include "compiler/CObjC.h"
#include "compiler/CObjCModern.h"
#include "compiler/CParser.h"
#include "compiler/CPrec.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CSOM.h"
#include "compiler/CTemplateClass.h"
#include "compiler/CTemplateFunc.h"
#include "compiler/CTemplateNew.h"
#include "compiler/CTemplateTools.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/DWARF.h"
#include "compiler/ELF_Endian.h"
#include "compiler/IROUseDef.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/IroBitVect.h"
#include "compiler/IroCSE.h"
#include "compiler/IroJump.h"
#include "compiler/IroLoop.h"
#include "compiler/IroVars.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/Switch.h"
#include "driver/Files.h"
#include "compiler/Objects.h"
#include "compiler/Types.h"

#undef CERROR_FILE
#define CERROR_FILE "CScope.c"
#undef CError_FATAL
#undef CError_ASSERT
#define CError_FATAL(line) CError_Internal("CScope.c", line)
#define CError_ASSERT(line, cond)                                                                                      \
    do {                                                                                                               \
        if (!(cond))                                                                                                   \
            CError_Internal("CScope.c", line);                                                                         \
    } while (0)

unsigned int CScope_ParseUsingDirective(NameSpace *container)
{
    NameSpaceList *entry;
    NameSpace *object;

    object = parse_namespace_name(container);
    if (object != container) {
        entry = container->usings;
        while (entry) {
            if (entry->nspace == object)
                break;
            entry = entry->next;
        }
        if (!entry) {
            entry = (NameSpaceList *)galloc(sizeof(NameSpaceList));
            entry->next = container->usings;
            entry->nspace = object;
            container->usings = entry;
        }
    } else {
        entry = CError_ReportError(ERR_ILLEGAL_USE_NAMESPACE_NAME);
    }
    if (tk != ';')
        entry = CError_ReportError(ERR_SEMICOLON_EXPECTED);
    return (unsigned int)entry;
}

void CScope_ParseUsingDeclaration(NameSpace *nspace, AccessType flag, Boolean unused)
{
    NameSpace *saveNSpace;
    Boolean isVirtual;
    Boolean consumedToken;
    NameSpaceObjectList *node;
    CScopeParseResult info;

    if (nspace->theclass != NULL) {
        isVirtual = (nspace->theclass->flags & Q_VIRTUAL) != 0;
        consumedToken = 0;
        if (tk == TK_TYPENAME) {
            if (!isVirtual)
                CError_ReportError(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
            consumedToken = 1;
            tk = CPrepTokenizer_GetNextToken();
        }
        if (!CScope_ParseMemberName(nspace->theclass, &info, isVirtual)) {
            CError_ReportError(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
            return;
        }
        if (info.type.base != NULL && info.type.base->type == TYPETEMPLATE &&
            ((TypeTemplDep *)info.type.base)->kind == 1) {
#if VERSION >= VERSION_GC_2_7
            CError_ASSERT(3578, isVirtual);
#else
            CError_ASSERT(3390, isVirtual);
#endif
            if (consumedToken) {
                ObjType *record = galloc(10);
                memclrw(record, 10);
                record->otype = OT_TYPE;
                record->access = flag;
                record->type = (Type *)info.type.base;
                CScope_AddObject(nspace, ((TypeTemplDep *)info.type.base)->u.qual.name, (ObjBase *)record);
            } else {
                CTemplateClass_AppendFuncDeclaration((TypeClassTemplate *)nspace->theclass,
                                                     ((TypeTemplDep *)info.type.base), flag);
            }
            tk = CPrepTokenizer_GetNextToken();
            if (tk != ';')
                CError_ReportError(ERR_SEMICOLON_EXPECTED);
            return;
        }
        if (info.is_qualified == 0) {
            CError_ReportError(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
            return;
        }
    } else {
        saveNSpace = currentNameSpace;
        currentNameSpace = nspace;
        if (!CScope_ParseExprName(&info) || info.is_qualified == 0) {
            currentNameSpace = saveNSpace;
            CError_ReportError(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
            return;
        }
        currentNameSpace = saveNSpace;
    }

    if (info.objects != NULL) {
        for (node = info.objects; node != NULL; node = node->next) {
            if (node->object->otype == OT_OBJECT)
                add_using_declaration(info.basePath, nspace, node->object, info.name, flag);
        }
    } else if (info.object != NULL) {
        add_using_declaration(info.basePath, nspace, info.object, info.name, flag);
    } else {
        CError_ReportError(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
    }
    tk = CPrepTokenizer_GetNextToken();
    if (tk != ';')
        CError_ReportError(ERR_SEMICOLON_EXPECTED);
}
#undef CERROR_FILE

static Boolean CScope_FixMemberResult(TypeClass *tclass, CScopeParseResult *result)
{
    result->basePath = CScope_GetClassAccessPath(result->basePath, tclass);
    if (result->basePath == NULL) {
        if (result->name != NULL)
            CError_ReportError(ERR_NOT_STRUCT_UNION_CLASS_MEMBER, result->name->name);
        else
            CError_ReportError(ERR_ILLEGAL_CLASS_MEMBER_ACCESS);
        return 0;
    }
    return 1;
}

void CScope_AddClassUsingDeclaration(TypeClass *def, TypeClass *tp, HashNameNode *name, Boolean flag)
{
    CScopeParseResult result;
    NameSpaceObjectList *scan;

    memclrw(&result, sizeof(result));
    if (!find_and_append_class_member_path(&result, tp->nspace, name, 0) || !CScope_FixMemberResult(def, &result)) {
        CError_ReportError(ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE, name->name);
        return;
    }

    if (result.objects) {
        for (scan = result.objects; scan; scan = scan->next) {
            switch (scan->object->otype) {
                case OT_ENUMCONST:
                case OT_MEMBERVAR:
                case OT_OBJECT:
                    add_using_declaration(result.basePath, def->nspace, scan->object, result.name, flag);
                    break;
            }
        }
    } else {
        if (result.object)
            add_using_declaration(result.basePath, def->nspace, result.object, result.name, flag);
        else
            CError_ReportError(ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE, name->name);
    }
}
#undef CERROR_FILE

#define CERROR_FILE "NameSpace.c"

static Boolean CScope_ObjectIsFunction(Object *obj)
{
    return obj->otype == OT_OBJECT && obj->type->type == TYPEFUNC;
}

static NameSpaceObjectList *CScope_FindQualName(NameSpace *nspace, HashNameNode *name)
{
    NameSpaceName *scan;

    if (!nspace->is_hash)
        scan = nspace->data.list;
    else
        scan = nspace->data.hash[name->hashval & 0x3ff];

    for (; scan; scan = scan->next) {
        if (scan->name == name)
            return &scan->first;
    }

    return NULL;
}

void add_using_declaration(BClassList *bases, NameSpace *scope, ObjBase *def, HashNameNode *name, char access)
{
    NameSpaceObjectList *lst;

    if (bases != NULL) {
        if (scope->theclass == NULL)
            CError_ReportError(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
        else
            CClass_CheckBaseAccess(bases,
#if VERSION >= VERSION_GC_2_7
                                   NULL,
#endif
                                   def->access);
    }

    if (def->otype == OT_TYPE) {
        if (scope->theclass == NULL) {
            if ((lst = CScope_FindQualName(scope, name)) != NULL) {
                if (lst->object->otype == OT_TYPE && ((ObjType *)def)->type == ((ObjType *)lst->object)->type &&
                    ((ObjType *)def)->qual == ((ObjType *)lst->object)->qual)
                    return;
            }
        }
        {
            ObjType *copy = (ObjType *)galloc(sizeof(ObjType));
            *copy = *(ObjType *)def;
            copy->access = access;
            CScope_AddObject(scope, name, (ObjBase *)copy);
        }
        return;
    }

    if (def->otype == OT_TYPETAG) {
        if (scope->theclass == NULL) {
            if ((lst = CScope_FindQualName(scope, name)) != NULL) {
                if (lst->object->otype == OT_TYPETAG &&
                    ((ObjNameSpace *)def)->nspace == ((ObjNameSpace *)lst->object)->nspace)
                    return;
            }
        }
        {
            ObjNameSpace *copy = (ObjNameSpace *)galloc(sizeof(ObjNameSpace));
            *copy = *(ObjNameSpace *)def;
            copy->access = access;
            CScope_AddObject(scope, name, (ObjBase *)copy);
        }
        return;
    }

    if (def->otype == OT_ENUMCONST) {
        ObjEnumConst *copy = (ObjEnumConst *)galloc(sizeof(ObjEnumConst));
        *copy = *(ObjEnumConst *)def;
        copy->access = access;
        CScope_AddObject(scope, copy->name, (ObjBase *)copy);
        return;
    }

    if (def->otype == OT_MEMBERVAR) {
        if (scope->theclass != NULL) {
            MemberVarAlias *copy = galloc(sizeof(MemberVarAlias));
            copy->member = *(ObjMemberVar *)def;
            copy->member.access = access;
            if ((bases = (BClassList *)CScope_GetClassAccessPath(CClass_GetPathCopy(bases, 1), scope->theclass)) !=
                    NULL &&
                bases->type == (Type *)scope->theclass) {
                copy->member.has_path = 1;
                copy->bases = bases;
            } else {
                CError_ReportError(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
            }
            CScope_AddObject(scope, copy->member.name, (ObjBase *)copy);
        } else {
            CError_ReportError(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
        }
        return;
    }

    if (def->otype == OT_OBJECT) {
        if (scope->theclass == NULL) {
            for (lst = CScope_FindQualName(scope, ((Object *)def)->name); lst != NULL; lst = lst->next) {
                if (lst->object->otype == OT_OBJECT) {
                    Object *target = (Object *)lst->object;
                    while (target->datatype == DALIAS)
                        target = target->u.alias.object;
                    if ((Object *)def == target)
                        return;
                }
            }
        }
        {
            Object *copy = (Object *)galloc(sizeof(Object));
            *copy = *(Object *)def;
            copy->access = access;
            copy->datatype = DALIAS;
            copy->u.alias.object = (Object *)def;
            copy->u.alias.member = NULL;
            copy->u.alias.offset = 0;
            if (((TypeMemberFunc *)copy->type)->type == TYPEFUNC &&
                (((TypeMemberFunc *)copy->type)->flags & FUNC_METHOD) && !((TypeMemberFunc *)copy->type)->is_static) {
                if (scope->theclass == NULL ||
                    (copy->u.alias.member = (BClassList *)CScope_GetClassAccessPath(CClass_GetPathCopy(bases, 1),
                                                                                    scope->theclass)) == NULL ||
                    copy->u.alias.member->type != (Type *)scope->theclass) {
                    CError_ReportError(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
                    copy->u.alias.member = NULL;
                }
            }
            CScope_AddObject(scope, copy->name, (ObjBase *)copy);
        }
        return;
    }

    CError_ReportError(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
    return;
}
#undef CERROR_FILE

#if VERSION >= VERSION_GC_2_7
static Boolean CScope_CheckDtorName(TypeClass *tclass, Boolean *flag)
{
    if ((tk = CPrepTokenizer_GetNextToken()) != TK_IDENTIFIER)
        CError_ReportError(ERR_IDENTIFIER_EXPECTED);

    if (tclass) {
        *flag = 0;
        if ((tclass->classname == data_00587fa0) ||
            (CScope_GetType(tclass->nspace, data_00587fa0, NULL) == (Type *)tclass) ||
            (CScope_GetType(currentNameSpace, data_00587fa0, NULL) == (Type *)tclass)) {
            if (!CClass_Destructor(tclass))
                *flag = 1;
            if ((tclass->flags & CLASS_IS_TEMPL_INST) && (CPrepTokenizer_GetNextTokenAndRestorePosition() == '<')) {
                tk = CPrepTokenizer_GetNextToken();
                if (!CTemplTool_EqualArgs(((TypeClassExt800 *)tclass)->targs,
                                          CTemplateNew_ParseTemplateArguments(NULL, 0)))
                    CError_ReportError(374);
            }
            return 1;
        }
    }

    CError_ReportError(ERR_EXPRESSION_SYNTAX_ERROR);
    return 0;
}
#endif

Boolean CScope_ParseMemberName(TypeClass *tclass, CScopeParseResult *result, Boolean flag)
{
    HashNameNode *name;
#if VERSION >= VERSION_GC_2_7
    NameSpaceObjectList *list;
    Boolean dtorflag;
#endif
    SInt16 t;

    if (tk == TK_COLON_COLON) {
    restart:
        if (!CScope_ParseExprName(result))
            return 0;

        if (result->type.base != NULL && result->type.base->type == TYPETEMPLATE &&
            ((TypeTemplDep *)result->type.base)->kind == 1) {
            if (flag)
                return 1;
            CError_ReportError(ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE,
                               ((TypeTemplDep *)result->type.base)->u.qual.name->name);
            result->type.base = NULL;
            return 0;
        }

        if (result->is_destructor)
            return 1;
        else
            return CScope_FixMemberResult(tclass, result);
    }

    if (tk == TK_IDENTIFIER) {
        name = data_00587fa0;
        t = CPrepTokenizer_GetNextTokenAndRestorePosition();
        data_00587fa0 = name;
        switch (t) {
            case TK_COLON_COLON:
                memclrw(result, sizeof(*result));
                if (!find_and_append_class_member_path(result, tclass->nspace, name, 2))
                    goto restart;
                break;
            case '<':
                if (flag)
                    goto restart;
                break;
        }
#if VERSION >= VERSION_GC_2_7
    } else if (tk == '~') {
        memclrw(result, sizeof(*result));
        if (CScope_CheckDtorName(tclass, &dtorflag)) {
            if (dtorflag) {
                result->is_destructor = 1;
                return 1;
            }

            if (!(list = find_namespace_object(result, tclass->nspace, destructor_name, &result->nspace))) {
                CError_ReportError(ERR_UNDEFINED_IDENTIFIER,
                                   CError_GetQualifiedHashName(result->nspace, destructor_name));
                return 0;
            } else {
                return set_parse_result_from_objects(result, list, destructor_name);
            }
        }
        return 0;
#endif
    }

    memclrw(result, sizeof(*result));
    return parse_name_in_namespace(result, tclass->nspace);
}
#undef CERROR_FILE

static inline BClassList *CScope_GetBaseAccessPathEnd(BClassList *list)
{
    BClassList *best = list;
    ClassList *scan;

    do {
        if (!list->next)
            return best;
        for (scan = TYPE_CLASS(list->type)->bases; scan; scan = scan->next) {
            if (TYPE_CLASS(list->next->type) == scan->base)
                break;
        }
        if (!scan)
            best = list->next;
        list = list->next;
    } while (1);
}

static inline BClassList *CScope_GetAccessPath(TypeClass *src, TypeClass *dest)
{
    class_path_base = src;
    found_class = NULL;
    return find_base_class_path(src, dest, 0);
}

BClassList *CScope_GetClassAccessPath(BClassList *list, TypeClass *tclass)
{
    BClassList *scan;
    BClassList *list2;

    if (!list)
        return NULL;
    list = CScope_GetBaseAccessPathEnd(list);
    for (scan = list; scan; scan = scan->next) {
        if (scan->type == (Type *)tclass)
            return scan;
    }
    if ((list2 = CScope_GetAccessPath(tclass, TYPE_CLASS(list->type)))) {
        for (scan = list2; scan; scan = scan->next) {
            if (scan->type == list->type) {
                scan->next = list->next;
                return list2;
            }
        }
#if VERSION >= VERSION_GC_2_7
        CError_FATAL(3217);
#else
        CError_FATAL(3053);
#endif
    }
    return NULL;
}

ObjectList *CScope_FindObjectListInNameSpace(NameSpace *nspace, HashNameNode *name)
{
    NameSpaceObjectList *nol;
    Object *obj;

    if ((nol = CScope_FindQualName(nspace, name)) != NULL) {
        obj = (Object *)nol->object;
        switch (obj->otype) {
            case OT_OBJECT:
                return remove_dalias_objects(nol);
            case OT_TYPETAG:
                break;
            default:
                CError_ReportError(ERR_IDENTIFIER_REDECLARED, name->name);
                return NULL;
        }
    }
    return NULL;
}
#undef CERROR_FILE

static NameSpaceObjectList *CScope_CopyList(NameSpaceObjectList *list)
{
    NameSpaceObjectList *first;
    NameSpaceObjectList *current;

    first = current = CompilerTools_AllocatePool(sizeof(NameSpaceObjectList));
    do {
        current->object = list->object;
        list = list->next;
        if (!list) {
            current->next = NULL;
            break;
        } else {
            current->next = CompilerTools_AllocatePool(sizeof(NameSpaceObjectList));
            current = current->next;
        }
    } while (1);

    return first;
}

ObjectList *remove_dalias_objects(NameSpaceObjectList *list)
{
    ObjectList *l;
    ObjectList *newlist;
    ObjectList **pp;
    ObjectList *p;

    l = (ObjectList *)list;
    if (l != NULL) {
        do {
            if (l->object.value->otype == OT_OBJECT && l->object.value->datatype == DALIAS) {
                newlist = (ObjectList *)CScope_CopyList(list);
                l = newlist;
                pp = &l;
                while ((p = *pp) != NULL) {
                    if (p->object.value->otype == OT_OBJECT && p->object.value->datatype == DALIAS)
                        *pp = p->next;
                    else
                        pp = &p->next;
                }
                return l;
            }
            l = l->next;
        } while (l != NULL);
    }
    return (ObjectList *)list;
}
#undef CERROR_FILE

static void CScope_NSIteratorInit(LookupCtx *iterator, NameSpace *nspace, CScopeParseResult *result)
{
#if VERSION >= VERSION_GC_2_7
    if (nspace->usings) {
#else
    if (nspace->usings && !result->is_qualified) {
#endif
        iterator->namespaceCursor = NULL;
        iterator->scopeChain = build_namespace_scope_rec(nspace);
    } else {
        iterator->namespaceCursor = nspace;
        iterator->scopeChain = NULL;
    }
    iterator->lookupState = result;
}

static Boolean CScope_NSIteratorNext(LookupCtx *iterator)
{
    if (iterator->scopeChain)
        return (iterator->scopeChain = iterator->scopeChain->outer) != NULL;
#if VERSION >= VERSION_GC_2_7
    if (iterator->lookupState->is_qualified)
        return 0;
#endif
    if ((iterator->namespaceCursor = iterator->namespaceCursor->parent)) {
        if (iterator->namespaceCursor->usings && !iterator->lookupState->is_qualified) {
            iterator->scopeChain = build_namespace_scope_rec(iterator->namespaceCursor);
            iterator->namespaceCursor = NULL;
        }
        return 1;
    }
    return 0;
}

Boolean CScope_FindTypeName(NameSpace *nspace, HashNameNode *name, CScopeParseResult *result)
{
    LookupCtx iterator;
    NameSpaceObjectList *list;
    NameSpaceObjectList *scan;

    memclrw(result, sizeof(*result));
    CScope_NSIteratorInit(&iterator, nspace, result);
    do {
        list = find_scope_object_list(&iterator, name);
        for (scan = list; scan; scan = scan->next) {
            switch (scan->object->otype) {
#if VERSION >= VERSION_GC_2_7
                case OT_NAMESPACE:
                    result->nspace = ((ObjNameSpace *)scan->object)->nspace;
                    return 1;
#endif
                case OT_TYPE:
                case OT_TYPETAG:
                    return set_parse_result_from_objects(result, scan, name);
                default:
                    return 0;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));
    return 0;
}
#undef CERROR_FILE

NameSpaceObjectList *CScope_FindName(NameSpace *nspace, HashNameNode *name)
{
    NameSpaceName *scan;

#if VERSION >= VERSION_GC_2_7
    for (scan = nspace->tparams; scan; scan = scan->next) {
        if (scan->name == name)
            return &scan->first;
    }
#endif

    if (!nspace->is_hash)
        scan = nspace->data.list;
    else
        scan = nspace->data.hash[name->hashval & 0x3ff];

    for (; scan; scan = scan->next) {
        if (scan->name == name)
            return &scan->first;
    }

    return NULL;
}

void CScope_ParseNameSpaceAlias(HashNameNode *name)
{
    NameSpaceObjectList *list;
    ObjNameSpace *objns;

    if (!(list = CScope_FindQualName(currentNameSpace, name))) {
        tk = CPrepTokenizer_GetNextToken();
        objns = (ObjNameSpace *)galloc(sizeof(ObjNameSpace));
        memclrw(objns, sizeof(ObjNameSpace));
        objns->otype = OT_NAMESPACE;
        objns->access = ACCESSPUBLIC;
        objns->nspace = parse_namespace_name(currentNameSpace);
        CScope_AddObject(currentNameSpace, name, (ObjBase *)objns);
    } else if (list->object->otype != OT_NAMESPACE) {
        CError_ReportError(ERR_ILLEGAL_NAMESPACE);
        tk = CPrepTokenizer_GetNextToken();
        (void)parse_namespace_name(currentNameSpace);
    } else {
        tk = CPrepTokenizer_GetNextToken();
        if (parse_namespace_name(currentNameSpace) != ((ObjNameSpace *)list->object)->nspace)
            CError_ReportError(ERR_IDENTIFIER_REDECLARED, name->name);
    }
    if (tk != ';')
        CError_ReportError(ERR_SEMICOLON_EXPECTED);
}

NameSpace *parse_namespace_name(NameSpace *nameSpace)
{
    CScopeParseResult result;
    LookupCtx iterator;
    NameSpaceObjectList *list;

    memclrw(&result, sizeof(result));
    if (tk == TK_COLON_COLON) {
        nameSpace = registration_context;
        result.is_qualified = 1;
        tk = CPrepTokenizer_GetNextToken();
    }

    do {
        if (tk != TK_IDENTIFIER) {
            CError_ReportError(ERR_IDENTIFIER_EXPECTED);
            break;
        }

        CScope_NSIteratorInit(&iterator, nameSpace, &result);
        do {
            if ((list = find_scope_object_list(&iterator, data_00587fa0)) && list->object->otype == OT_NAMESPACE) {
                nameSpace = ((ObjNameSpace *)list->object)->nspace;
                goto some_goto;
            }
        } while (CScope_NSIteratorNext(&iterator));
        CError_ReportError(ERR_UNDEFINED_IDENTIFIER, data_00587fa0->name);
    some_goto:
        if ((tk = CPrepTokenizer_GetNextToken()) != TK_COLON_COLON)
            break;
        result.is_qualified = 1;
        tk = CPrepTokenizer_GetNextToken();
    } while (1);

    return nameSpace;
}

Type *CScope_GetTagType(NameSpace *nspace, HashNameNode *name)
{
    NameSpaceObjectList *list;

    for (list = CScope_FindQualName(nspace, name); list; list = list->next) {
        if (list->object->otype == OT_TYPETAG)
            return ((ObjType *)list->object)->type;
    }
    return NULL;
}

void CScope_DefineTypeTag(NameSpace *ns, HashNameNode *name, Type *type)
{
    ObjType *tag = galloc(6);
    UInt8 access;
    memclrw(tag, 6);
    tag->otype = OT_TYPETAG;
    if (ns->theclass != NULL)
        access = member_access;
    else
        access = 0;
    tag->access = access;
    tag->type = type;
    CScope_AddObject(ns, name, (ObjBase *)tag);
}
#undef CERROR_FILE

NameSpaceObjectList *CScope_NextNameSpaceObjectList(ScopeSearch *state)
{
    NameSpaceName *entry;

    for (;;) {
        if ((entry = state->nextName) != NULL) {
            state->nextName = entry->next;
            return &entry->first;
        }
        if (state->owner->is_hash == 0 || ++state->bucketIndex >= 0x400)
            return NULL;
        state->nextName = state->owner->data.hash[state->bucketIndex];
    }
}
#undef CERROR_FILE

Object *CScope_NextObject(ScopeSearch *s)
{
    while (1) {
        if (s->nextObject != NULL) {
            do {
                ObjBase *obj = s->nextObject->object;
                if (obj->otype == OT_OBJECT) {
                    s->nextObject = s->nextObject->next;
                    return (Object *)obj;
                }
                s->nextObject = s->nextObject->next;
            } while (s->nextObject != NULL);
        }
        if (s->nextName != NULL) {
            s->nextObject = &s->nextName->first;
            s->nextName = s->nextName->next;
            continue;
        }
        if (s->owner->is_hash == 0 || ++s->bucketIndex >= 0x400)
            return NULL;
        s->nextName = s->owner->data.hash[s->bucketIndex];
    }
}
#undef CERROR_FILE

int CScope_InitScopeSearch(ScopeSearch *save, NameSpace *obj)
{
    memclrw(save, sizeof(*save));
    save->owner = obj;
    if (save->owner->is_hash == 0)
        save->nextName = obj->data.list;
    else
        save->nextName = *obj->data.hash;
}
#undef CERROR_FILE

Boolean CScope_PossibleTypeName(HashNameNode *name)
{
    LookupCtx iterator;
    CScopeParseResult result;
    NameSpaceObjectList *list;

    memclrw(&result, sizeof(result));
    CScope_NSIteratorInit(&iterator, currentNameSpace, &result);
    do {
        if ((list = find_scope_object_list(&iterator, name))) {
            switch (list->object->otype) {
                case OT_TYPE:
                case OT_NAMESPACE:
                    return 1;
                case OT_TYPETAG:
                    if (copts.cplusplus)
                        return 1;
                    break;
                default:
                    return 0;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return 0;
}
#undef CERROR_FILE

#define CERROR_FILE "CScopeParseResult.c"

NameSpaceObjectList *CScope_FindObjectList(CScopeParseResult *result, HashNameNode *name)
{
    LookupCtx iterator;
    NameSpaceObjectList *list;

    memclrw(result, sizeof(*result));
    CScope_NSIteratorInit(&iterator, currentNameSpace, result);
    do {
        for (list = find_scope_object_list(&iterator, name); list; list = list->next) {
            if (copts.cplusplus || list->object->otype != OT_TYPETAG) {
                result->nspace = iterator.scopeChain ? iterator.scopeChain->ns : iterator.namespaceCursor;
                return list;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return 0;
}

static NameSpaceObjectList *CScope_NSIteratorFind(LookupCtx *iterator, HashNameNode *name)
{
    NameSpaceObjectList *list;

    if (iterator->scopeChain) {
        if (iterator->scopeChain->list)
            return CScope_0049a000(iterator->scopeChain, name, NULL);
        if (iterator->scopeChain->ns->theclass)
            return NULL;
        if ((list = CScope_FindName(iterator->scopeChain->ns, name)))
            return list;
    } else {
        if (iterator->namespaceCursor->theclass)
            return NULL;
        if ((list = CScope_FindName(iterator->namespaceCursor, name)))
            return list;
    }
    return NULL;
}

Boolean CScope_FindObject(NameSpace *nspace, CScopeParseResult *result, HashNameNode *name)
{
    NameSpaceObjectList *list;
    LookupCtx iterator;

    memclrw(result, sizeof(*result));
    CScope_NSIteratorInit(&iterator, nspace, result);
    do {
        for (list = CScope_NSIteratorFind(&iterator, name); list; list = list->next) {
            if (copts.cplusplus || list->object->otype != OT_TYPETAG) {
                result->nspace = iterator.scopeChain ? iterator.scopeChain->ns : iterator.namespaceCursor;
                return set_parse_result_from_objects(result, list, name);
            }
        }
    } while (CScope_NSIteratorNext(&iterator));
    return 0;
}

Boolean CScope_ParseElaborateName(CScopeParseResult *result)
{
    LookupCtx iterator;
    HashNameNode *name;
    NameSpaceObjectList *list;

    if (!copts.cplusplus) {
        memclrw(result, sizeof(*result));
        if (tk != TK_IDENTIFIER) {
            CError_ReportError(ERR_IDENTIFIER_EXPECTED);
            return 0;
        }

        name = data_00587fa0;
        CScope_NSIteratorInit(&iterator, currentNameSpace, result);
        do {
            for (list = find_scope_object_list(&iterator, name); list; list = list->next) {
                if (list->object->otype == OT_TYPETAG) {
                    result->nspace = iterator.scopeChain ? iterator.scopeChain->ns : iterator.namespaceCursor;
                    return set_parse_result_from_objects(result, list, name);
                }
            }
        } while (CScope_NSIteratorNext(&iterator));

        result->name = name;
        return 1;
    } else {
        if (tk != TK_COLON_COLON && tk != TK_IDENTIFIER) {
            CError_ReportError(ERR_IDENTIFIER_EXPECTED);
            return 0;
        }

#if VERSION >= VERSION_GC_2_7
        if (!CScope_ParseQualifiedScope(result, 0, 0)) {
#else
        if (!CScope_ParseQualifiedScope(result, 0)) {
#endif
            result->nspace = currentNameSpace;
            if (tk != TK_IDENTIFIER) {
                CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                return 0;
            }
            name = data_00587fa0;
        } else {
            if (result->type.base)
                return 1;
#if VERSION >= VERSION_GC_2_7
            CError_ASSERT(2760, result->nspace);
#else
            CError_ASSERT(2600, result->nspace);
#endif

            if (tk != TK_IDENTIFIER) {
                CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                return 0;
            }
            name = data_00587fa0;

            if (result->is_qualified) {
                if (result->nspace->theclass) {
                    if (find_and_append_class_member_path(result, result->nspace, name, 1))
                        return 1;
                    return 0;
                }
                return find_type_name_in_scope(result, result->nspace, name);
            }
        }

        CScope_NSIteratorInit(&iterator, result->nspace, result);
        do {
            for (list = find_scope_object_list(&iterator, name); list; list = list->next) {
                if (list->object->otype == OT_TYPETAG || list->object->otype == OT_TYPE) {
                    result->nspace = iterator.scopeChain ? iterator.scopeChain->ns : iterator.namespaceCursor;
                    return set_parse_result_from_objects(result, list, name);
                }
            }
        } while (CScope_NSIteratorNext(&iterator));

        result->name = name;
        return 1;
    }
}

#define TCE(t) ((TypeClassExt800 *)(t))

#if VERSION >= VERSION_GC_2_7
Boolean CScope_ParseQualifiedScope(CScopeParseResult *result, Boolean flag1, Boolean flag2)
#else
Boolean CScope_ParseQualifiedScope(CScopeParseResult *result, SInt32 flag)
#endif
{
#if VERSION >= VERSION_GC_2_7
    HashNameNode *name;
    NameSpace *nspace;
    SInt16 t;
    Type *type;
    Type *type2;
    NameSpaceObjectList *list;
    LookupCtx iterator;
#else
    Type *type;
    HashNameNode *name;
    NameSpace *nspace;
    SInt32 tokenValue;
    LookupCtx iterator;
    SInt16 t;
    NameSpaceObjectList *list;
    Type *type2;
#endif

    memclrw(result, sizeof(*result));
    nspace = NULL;
    if (tk == TK_COLON_COLON) {
        result->nspace = nspace = registration_context;
        result->is_qualified = 1;
        tk = CPrepTokenizer_GetNextToken();
    }

restart:
    if (tk != TK_IDENTIFIER) {
#if VERSION >= VERSION_GC_2_7
        if (tk != TK_TEMPLATE)
            return nspace != NULL;
        if (!result->is_qualified)
            CError_ReportError(ERR_ILLEGAL_USE_TEMPLATE_PREFIX);
        if ((tk = CPrepTokenizer_GetNextToken()) != TK_IDENTIFIER) {
            CError_ReportError(ERR_IDENTIFIER_EXPECTED);
            return 0;
        }
#else
        return nspace != NULL;
#endif
    }

    name = data_00587fa0;
    t = CPrepTokenizer_GetNextTokenAndRestorePosition();
    data_00587fa0 = name;

#if VERSION >= VERSION_GC_2_7
    if (t != TK_COLON_COLON && t != '<')
#else
    tokenValue = t;
    if (tokenValue != TK_COLON_COLON && t != '<')
#endif
        return nspace != NULL;

    CScope_NSIteratorInit(&iterator, nspace ? nspace : currentNameSpace, result);
    do {
        for (list = find_scope_object_list(&iterator, name); list; list = list->next) {
            if (list->object->otype == OT_NAMESPACE) {
#if VERSION >= VERSION_GC_2_7
                CError_ASSERT(2565, !nspace || !nspace->theclass);
#else
                CError_ASSERT(2423, !nspace || !nspace->theclass);
#endif

                result->nspace = nspace = ((ObjNameSpace *)list->object)->nspace;
                if ((tk = CPrepTokenizer_GetNextToken()) != TK_COLON_COLON) {
                    CError_ReportError(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                    return 0;
                }

                result->is_qualified = 1;
                tk = CPrepTokenizer_GetNextToken();
                goto restart;
            }
            if (list->object->otype == OT_TYPETAG) {
                type = ((ObjType *)list->object)->type;
                if (type->type != TYPECLASS) {
                    if (t == '<') {
                        result->type.base = type;
                        return 1;
                    } else {
                        CError_ReportError(ERR_DECLARATION_SYNTAX_ERROR);
                        return 0;
                    }
                }
            parse_thing:
                if (t == '<') {
                    if (TCE(type)->base.flags & CLASS_IS_TEMPL_INST) {
                        type = TCE(type)->classTemplate;
                    } else if (!(TCE(type)->base.flags & CLASS_IS_TEMPL)) {
                        result->type.base = type;
                        return 1;
                    }
                }
                if ((tk = CPrepTokenizer_GetNextToken()) == '<') {
#if VERSION >= VERSION_GC_2_7
                    CError_ASSERT(2609, TCE(type)->base.flags & CLASS_IS_TEMPL);
#else
                    CError_ASSERT(2467, TCE(type)->base.flags & CLASS_IS_TEMPL);
#endif
                    type2 = CTemplTool_GetSelfRefTemplate((TypeClassTemplate *)type);
                    if (type2->type == TYPETEMPLATE) {
                        if (CPrepTokenizer_GetNextTokenAndRestorePosition() != TK_COLON_COLON) {
                            result->type.base = type2;
                            return 1;
                        }
#if VERSION >= VERSION_GC_2_7
                        return parse_qualified_templdep_type(result, type2, flag1, flag2);
#else
                        return parse_qualified_templdep_type(result, type2, flag);
#endif
                    } else if (type2->type != TYPECLASS) {
                        return 0;
                    }

                    result->nspace = nspace = TCE(type2)->base.nspace;
                    if (CPrepTokenizer_GetNextTokenAndRestorePosition() != TK_COLON_COLON) {
                        result->type.base = type2;
                        return 1;
                    }
                    tk = CPrepTokenizer_GetNextToken();
                    CDecl_CompleteType(type2);
                } else {
#if VERSION >= VERSION_GC_2_7
                    CError_ASSERT(2632, tk == TK_COLON_COLON);
                    if (!(TCE(type)->base.flags & CLASS_IS_TEMPL) || CParser_CheckTemplateClassScope(type, 1))
#else
                    CError_ASSERT(2490, tk == TK_COLON_COLON);
                    if (!(TCE(type)->base.flags & CLASS_IS_TEMPL) || CParser_CheckTemplateClassScope(type))
#endif
                        result->nspace = nspace = TCE(type)->base.nspace;
#if VERSION >= VERSION_GC_2_7
                    CDecl_CompleteType(type);
#endif
                }

                result->is_qualified = 1;
                tk = CPrepTokenizer_GetNextToken();
                goto restart;
            }
            if (list->object->otype == OT_TYPE) {
                type2 = ((ObjType *)list->object)->type;
                if (type2->type != TYPECLASS) {
#if VERSION >= VERSION_GC_2_7
                    if (type2->type == TYPETEMPLATE) {
                        if (((TypeTemplDep *)type2)->kind == TEMPLDEP_ARGUMENT &&
                            ((TypeTemplDep *)type2)->u.pid.type == TPT_TEMPLATE) {
                            type2 = CTempl_ParseTemplTemplParam((TypeTemplDep *)type2);
                            if (type2->type != TYPETEMPLATE ||
                                (t = CPrepTokenizer_GetNextTokenAndRestorePosition()) != TK_COLON_COLON) {
                                result->type.base = type2;
                                return 1;
                            }
                        }
                        if (t == TK_COLON_COLON)
                            return parse_qualified_templdep_type(result, type2, flag1, flag2);
                    }
#else
                    if (tokenValue == TK_COLON_COLON && type2->type == TYPETEMPLATE)
                        return parse_qualified_templdep_type(result, type2, flag);
#endif
                    if (t == '<') {
                        result->type.base = type2;
                        return 1;
                    } else {
                        CError_ReportError(ERR_DECLARATION_SYNTAX_ERROR);
                        return 0;
                    }
                } else {
#if VERSION >= VERSION_GC_2_7
                    if (t == '<' && (TCE(type2)->base.flags & CLASS_IS_TEMPL)) {
                        type = type2;
                        goto parse_thing;
                    }
#endif
                    if ((tk = CPrepTokenizer_GetNextToken()) == '<') {
                        result->type.base = type2;
                        return 1;
                    }
#if VERSION >= VERSION_GC_2_7
                    CError_ASSERT(2686, tk == TK_COLON_COLON);
#else
                    CError_ASSERT(2525, tk == TK_COLON_COLON);
#endif
                    if (!type2->size)
                        CDecl_CompleteType(type2);
                    result->nspace = nspace = TCE(type2)->base.nspace;
                    result->is_qualified = 1;
                    tk = CPrepTokenizer_GetNextToken();
                    goto restart;
                }
            }
            if (t == '<')
                return nspace != NULL;
        }
    } while (CScope_NSIteratorNext(&iterator));

    CError_ReportError(ERR_UNDEFINED_IDENTIFIER, name->name);
    return 0;
}
#undef CERROR_FILE

void CScope_RestoreScope(CScopeSave *save)
{
    currentNameSpace = save->nspace;
    data_00588040 = save->theclass;
    data_00588238 = save->function;
    data_005884f8 = save->member_context;
}

static inline void CScope_SetNameSpaceScope(NameSpace *nspace, CScopeSave *save)
{
    save->nspace = currentNameSpace;
    save->theclass = data_00588040;
    save->function = data_00588238;
    save->member_context = data_005884f8;

    currentNameSpace = nspace;
    data_00588040 = nspace->theclass;
    data_00588238 = NULL;
    data_005884f8 = 0;
}

Boolean CScope_ParseDeclName(CScopeParseResult *result)
{
    HashNameNode *name;
    LookupCtx iterator;
    NameSpace *nspace;
    NameSpaceObjectList *list;
#if VERSION >= VERSION_GC_2_7
    Boolean flag;
    CScopeSave save;
    SInt16 op;
#endif

    if (!copts.cplusplus) {
    non_cpp_mode:
        if (tk != TK_IDENTIFIER) {
            CError_ReportError(ERR_IDENTIFIER_EXPECTED);
            return 0;
        }
        memclrw(result, sizeof(*result));

        name = data_00587fa0;
        CScope_NSIteratorInit(&iterator, currentNameSpace, result);
        do {
            if ((list = find_scope_object_list(&iterator, name)) &&
                (copts.cplusplus || (list->object->otype != OT_TYPETAG))) {
                result->nspace = iterator.scopeChain != NULL ? iterator.scopeChain->ns : iterator.namespaceCursor;
                return set_parse_result_from_objects(result, list, name);
            }
        } while (CScope_NSIteratorNext(&iterator));

        result->nspace = currentNameSpace;
        result->name = name;
        return 0;
    } else {
        if ((tk != TK_COLON_COLON) && (tk != TK_IDENTIFIER)) {
            CError_ReportError(ERR_IDENTIFIER_EXPECTED);
            return 0;
        }

#if VERSION >= VERSION_GC_2_7
        if (!CScope_ParseQualifiedScope(result, 0, 0))
#else
        if (!CScope_ParseQualifiedScope(result, 0))
#endif
            goto non_cpp_mode;

        if (result->type.base)
            return 1;

#if VERSION >= VERSION_GC_2_7
        CError_ASSERT(2435, (nspace = result->nspace) != NULL);
#else
        CError_ASSERT(2305, (nspace = result->nspace) != NULL);
#endif

        switch (tk) {
            case TK_OPERATOR:
#if VERSION >= VERSION_GC_2_7
                CScope_SetNameSpaceScope(nspace, &save);
                if (!CParser_00490660(&op, 1, 1)) {
                    CScope_RestoreScope(&save);
                    return 0;
                }
                CScope_RestoreScope(&save);
                if (op)
                    CError_ReportError(ERR_DECLARATION_SYNTAX_ERROR);
                result->is_operator = 1;
                return 1;
#else
            {
                CScopeSave save;
                CScope_SetNameSpaceScope(nspace, &save);
                if (!CParser_00490660(NULL, 1)) {
                    CScope_RestoreScope(&save);
                    return 0;
                }
                CScope_RestoreScope(&save);
                tk = TK_IDENTIFIER;
                name = data_00587fa0;
                CPrep_UngetToken();
                break;
            }
#endif
            case TK_IDENTIFIER:
                name = data_00587fa0;
                if (nspace->theclass && nspace->theclass->classname == name)
                    name = constructor_name;
                break;
            case TK_COMPL:
                if (!nspace->theclass) {
                    CError_ReportError(ERR_DECLARATION_SYNTAX_ERROR);
                    return 0;
                }
                tk = CPrepTokenizer_GetNextToken();
                if (tk != TK_IDENTIFIER) {
                    CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                    return 0;
                }
                if (nspace->theclass->classname != data_00587fa0)
                    CError_ReportError(ERR_DECLARATION_SYNTAX_ERROR);
                name = destructor_name;
                break;
            default:
                CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                return 0;
        }

        if (result->is_qualified) {
            if (!(list = find_namespace_object(result, result->nspace, name, &result->nspace))) {
                CError_ReportError(ERR_UNDEFINED_IDENTIFIER, name->name);
                return 0;
            } else {
                return set_parse_result_from_objects(result, list, name);
            }
        }

        CScope_NSIteratorInit(&iterator, nspace, result);
        do {
            if ((list = find_scope_object_list(&iterator, name))) {
                result->nspace = iterator.scopeChain != NULL ? iterator.scopeChain->ns : iterator.namespaceCursor;
                return set_parse_result_from_objects(result, list, name);
            }
        } while (CScope_NSIteratorNext(&iterator));

        CError_ReportError(ERR_UNDEFINED_IDENTIFIER, name->name);
        return 0;
    }
}

Boolean CScope_ParseExprName(CScopeParseResult *result)
{
    LookupCtx iterator;
    HashNameNode *name;
    NameSpaceObjectList *list;
#if VERSION >= VERSION_GC_2_7
    Boolean flag;
#endif

    if (!copts.cplusplus) {
        memclrw(result, sizeof(*result));
        if (tk != TK_IDENTIFIER) {
            CError_ReportError(ERR_IDENTIFIER_EXPECTED);
            return 0;
        }

        name = data_00587fa0;
        CScope_NSIteratorInit(&iterator, currentNameSpace, result);
        do {
            if ((list = find_scope_object_list(&iterator, name)) && (list->object->otype != OT_TYPETAG)) {
                result->nspace = iterator.scopeChain ? iterator.scopeChain->ns : iterator.namespaceCursor;
                return set_parse_result_from_objects(result, list, name);
            }
        } while (CScope_NSIteratorNext(&iterator));

        result->nspace = currentNameSpace;
        result->name = name;
        return 1;
    } else {
#if VERSION >= VERSION_GC_2_7
        if ((tk == TK_COLON_COLON || tk == TK_IDENTIFIER) && CScope_ParseQualifiedScope(result, 1, 1)) {
            if (result->type.base)
                return 1;
            CError_ASSERT(2313, result->nspace);
        } else {
#else
        if ((tk == TK_COLON_COLON || tk == TK_IDENTIFIER) && CScope_ParseQualifiedScope(result, 1)) {
            if (result->type.base)
                return 1;
            CError_ASSERT(2185, result->nspace);
        } else {
#endif
            memclrw(result, sizeof(*result));
            result->nspace = currentNameSpace;
        }

        switch (tk) {
#if VERSION >= VERSION_GC_2_7
            case TK_TEMPLATE:
                if (!result->is_qualified)
                    CError_ReportError(ERR_ILLEGAL_USE_TEMPLATE_PREFIX);

                if ((tk = CPrepTokenizer_GetNextToken()) != TK_IDENTIFIER) {
                    CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                    return 0;
                }
#endif
            case TK_IDENTIFIER:
                name = data_00587fa0;
                break;
            case TK_OPERATOR:
#if VERSION >= VERSION_GC_2_7
                if (!CParser_00490660(NULL, 1, 1))
#else
                if (!CParser_00490660(NULL, 1))
#endif
                    return 0;
                name = data_00587fa0;
                CPrep_UngetToken();
                break;
            case TK_COMPL:
#if VERSION >= VERSION_GC_2_7
                if (!CScope_CheckDtorName(result->nspace->theclass, &flag))
                    return 0;
                if (flag) {
                    result->is_destructor = 1;
                    return 1;
                }
                name = destructor_name;
                break;
#else
                if (result->nspace->theclass != NULL) {
                    tk = CPrepTokenizer_GetNextToken();
                    if (tk == TK_IDENTIFIER) {
                        if (result->nspace->theclass->classname == data_00587fa0 ||
                            CScope_GetType(currentNameSpace, data_00587fa0, NULL) == (Type *)result->nspace->theclass) {
                            if (CClass_Destructor(result->nspace->theclass) == NULL) {
                                result->is_destructor = 1;
                                return 1;
                            }
                            name = destructor_name;
                            break;
                        }
                        CError_ReportError(ERR_EXPRESSION_SYNTAX_ERROR);
                        return 0;
                    }
                }
                CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                return 0;
#endif
            default:
                CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                return 0;
        }

        if (result->is_qualified) {
            if (!(list = find_namespace_object(result, result->nspace, name, &result->nspace))) {
                CError_ReportError(ERR_UNDEFINED_IDENTIFIER, CError_GetQualifiedHashName(result->nspace, name));
                return 0;
            } else {
                return set_parse_result_from_objects(result, list, name);
            }
        }

        CScope_NSIteratorInit(&iterator, result->nspace, result);
        do {
            if ((list = find_scope_object_list(&iterator, name))) {
                result->nspace = iterator.scopeChain ? iterator.scopeChain->ns : iterator.namespaceCursor;
                return set_parse_result_from_objects(result, list, name);
            }
        } while (CScope_NSIteratorNext(&iterator));

        if (result->is_qualified) {
            CError_ReportError(ERR_UNDEFINED_IDENTIFIER, name->name);
            return 0;
        } else {
            result->nspace = currentNameSpace;
            result->name = name;
            return 1;
        }
    }
}

Boolean parse_name_in_namespace(CScopeParseResult *result, NameSpace *nspace)
{
    NameSpaceObjectList *list;
    HashNameNode *saveidentifier;
    TypeClassTemplate *tclass;
    Boolean flag = 0;

    do {
        switch (tk) {
#if VERSION >= VERSION_GC_2_7
            case TK_TEMPLATE:
                if ((tk = CPrepTokenizer_GetNextToken()) != TK_IDENTIFIER) {
                    CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                    return 0;
                }
#endif
            case TK_IDENTIFIER:
                saveidentifier = data_00587fa0;
                if (CPrepTokenizer_GetNextTokenAndRestorePosition() == TK_COLON_COLON) {
                    tk = CPrepTokenizer_GetNextToken();
                    nspace = find_name_nspace(result, nspace, saveidentifier);
                    if (!nspace)
                        return 0;
                    result->is_qualified = 1;
                    tk = CPrepTokenizer_GetNextToken();
                    continue;
                }
                break;
            case TK_OPERATOR:
#if VERSION >= VERSION_GC_2_7
                if (!CParser_00490660(NULL, 1, 1))
#else
                if (!CParser_00490660(NULL, 1))
#endif
                    return 0;
                CPrep_UngetToken();
                saveidentifier = data_00587fa0;
                break;
            case TK_COMPL:
#if VERSION >= VERSION_GC_2_7
                if (!CScope_CheckDtorName(nspace->theclass, &flag))
                    return 0;
                saveidentifier = destructor_name;
                flag = 1;
                break;
#else
                if (nspace->theclass != NULL) {
                    tk = CPrepTokenizer_GetNextToken();
                    if (tk == TK_IDENTIFIER) {
                        if (nspace->theclass->classname == data_00587fa0 ||
                            CScope_GetType(currentNameSpace, data_00587fa0, NULL) == (Type *)nspace->theclass) {
                            saveidentifier = destructor_name;
                            flag = 1;
                            break;
                        } else {
                            CError_ReportError(ERR_EXPRESSION_SYNTAX_ERROR);
                            return 0;
                        }
                    }
                }
                CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                return 0;
#endif
            default:
                CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                return 0;
        }

        if (!(list = find_namespace_object(result, nspace, saveidentifier, &result->nspace)) ||
            !set_parse_result_from_objects(result, list, saveidentifier)) {
            if (flag) {
                result->is_destructor = 1;
                return 1;
            }
            if (nspace->theclass && !(nspace->theclass->flags & CLASS_COMPLETED))
                CError_ReportError(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, nspace->theclass, 0);
            else
                CError_ReportError(ERR_UNDEFINED_IDENTIFIER, saveidentifier->name);
            return 0;
        }

        if (result->type.base && result->type.base->type == TYPECLASS &&
            CPrepTokenizer_GetNextTokenAndRestorePosition() == '<') {
            tclass = (TypeClassTemplate *)result->type.base;
            if (tclass->base.flags & CLASS_IS_TEMPL_INST)
                tclass = (TypeClassTemplate *)((TypeClassExt800 *)tclass)->classTemplate;
            else if (!(tclass->base.flags & CLASS_IS_TEMPL))
                return 1;

            tk = CPrepTokenizer_GetNextToken();
            result->type.base = CTemplTool_GetSelfRefTemplate(tclass);
            if (result->type.base->type == TYPECLASS &&
                CPrepTokenizer_GetNextTokenAndRestorePosition() == TK_COLON_COLON) {
                CPrepTokenizer_GetNextToken();
                tk = CPrepTokenizer_GetNextToken();
                result->is_qualified = 1;
                nspace = ((TypeClass *)result->type.base)->nspace;
                result->type.base = NULL;
                result->object = NULL;
                continue;
            }
        }

        return 1;
    } while (1);
}
#undef CERROR_FILE

#if VERSION >= VERSION_GC_2_7
Boolean parse_qualified_templdep_type(CScopeParseResult *context, Type *qualifier, Boolean flag1, Boolean flag2)
#else
Boolean parse_qualified_templdep_type(CScopeParseResult *context, Type *qualifier, Boolean allowToken328)
#endif
{
    TypeTemplDep *node;
    TypeTemplDep *qualifiedType;
    SInt16 token;
    SInt32 savedState;

    CPrep_GetBufferedTokenPosition(&savedState);
#if VERSION >= VERSION_GC_2_7
    CError_ASSERT(2026, CPrepTokenizer_GetNextToken() == TK_COLON_COLON);
#else
    CError_ASSERT(1967, CPrepTokenizer_GetNextToken() == TK_COLON_COLON);
#endif
    for (;;) {
        token = CPrepTokenizer_GetNextToken();
#if VERSION >= VERSION_GC_2_7
        if (token == TK_OPERATOR && flag1) {
#else
        if (token == TK_OPERATOR && allowToken328) {
#endif
#if VERSION >= VERSION_GC_2_7
            if (!CParser_00490660(NULL, 1, 1))
#else
            if (!CParser_00490660(NULL, 1))
#endif
                return 0;
            node = CDecl_NewTemplDepType(1);
            node->u.qual.type = (TypeTemplDep *)qualifier;
            node->u.qual.name = data_00587fa0;
            CPrep_SetBufferedTokenPosition(&savedState);
            CPrepTokenizer_GetNextToken();
            tk = CPrepTokenizer_GetNextToken();
            context->type.base = (Type *)node;
            return 1;
        }

#if VERSION >= VERSION_GC_2_7
        if (token == TK_TEMPLATE) {
            if (CPrepTokenizer_GetNextToken() != TK_IDENTIFIER) {
                CError_ReportError(ERR_IDENTIFIER_EXPECTED);
                return 0;
            }

            CPrep_GetBufferedTokenPosition(&savedState);
            node = CDecl_NewTemplDepType(1);
            node->u.qual.type = (TypeTemplDep *)qualifier;
            node->u.qual.name = data_00587fa0;
            if ((token = CPrepTokenizer_GetNextToken()) != '<') {
                CError_ReportError(ERR_LESS_EXPECTED);
                return 0;
            }
            tk = token;
            qualifiedType = CDecl_NewTemplDepType(4);
            qualifiedType->u.qualtempl.type = node;
            qualifiedType->u.qualtempl.args = CTemplateNew_ParseTemplateArguments(NULL, 1);
            CPrep_GetBufferedTokenPosition(&savedState);
            if (CPrepTokenizer_GetNextToken() == TK_COLON_COLON) {
                qualifier = (Type *)qualifiedType;
                continue;
            } else {
                CPrep_SetBufferedTokenPosition(&savedState);
                context->type.base = (Type *)qualifiedType;
                return 1;
            }
        }
#endif

        if (token == TK_IDENTIFIER) {
            node = CDecl_NewTemplDepType(1);
            node->u.qual.type = (TypeTemplDep *)qualifier;
            node->u.qual.name = data_00587fa0;
            tk = token;

            CPrep_GetBufferedTokenPosition(&savedState);
            token = CPrepTokenizer_GetNextToken();
            data_00587fa0 = node->u.qual.name;
            if (token == TK_COLON_COLON) {
                qualifier = (Type *)node;
                continue;
            }

#if VERSION >= VERSION_GC_2_7
            if (token == '<' && !flag2) {
#else
            if (token == '<') {
#endif
                tk = token;
                qualifiedType = node;
                node = CDecl_NewTemplDepType(4);
                node->u.qualtempl.type = qualifiedType;
                node->u.qualtempl.args = CTemplateNew_ParseTemplateArguments(NULL, 1);

                CPrep_GetBufferedTokenPosition(&savedState);
                if (CPrepTokenizer_GetNextToken() == TK_COLON_COLON) {
                    qualifier = (Type *)node;
                    continue;
                }
            }

            CPrep_SetBufferedTokenPosition(&savedState);
            context->type.base = (Type *)node;
            return 1;
        }

        break;
    }

    CPrep_SetBufferedTokenPosition(&savedState);
    context->type.base = qualifier;
    return 1;
}
#undef CERROR_FILE

Type *CScope_FindTagType(NameSpace *nspace, HashNameNode *name)
{
    LookupCtx iterator;
    CScopeParseResult result;
    NameSpaceObjectList *list;

    memclrw(&result, sizeof(result));
    CScope_NSIteratorInit(&iterator, nspace, &result);

    do {
        for (list = find_scope_object_list(&iterator, name); list; list = list->next) {
            if (list->object->otype == OT_TYPETAG)
                return ((ObjType *)list->object)->type;
        }
    } while (CScope_NSIteratorNext(&iterator));

    return NULL;
}

Type *CScope_GetType(NameSpace *nspace, HashNameNode *name, UInt32 *qual)
{
    LookupCtx iterator;
    CScopeParseResult result;
    NameSpaceObjectList *list;

    memclrw(&result, sizeof(result));
    CScope_NSIteratorInit(&iterator, nspace, &result);

    do {
        for (list = find_scope_object_list(&iterator, name); list; list = list->next) {
            if (list->object->otype == OT_TYPETAG) {
                if (qual)
                    *qual = 0;
                return ((ObjType *)list->object)->type;
            }
            if (list->object->otype == OT_TYPE) {
                if (qual)
                    *qual = ((ObjType *)list->object)->qual;
                return ((ObjType *)list->object)->type;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return NULL;
}
#undef CERROR_FILE

static void AmbiguousError(NameSpace *scope, NameSpace *ns, HashNameNode *name)
{
    if (name != NULL && scope != ns)
        CError_ReportError(ERR_AMBIGUOUS_ACCESS_NAME_FOUND, CError_GetQualifiedHashName(scope, name),
                           CError_GetQualifiedHashName(ns, name));
    else
        CError_ReportError(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
}

Boolean find_type_name_in_scope(CScopeParseResult *out, NameSpace *scope, HashNameNode *name)
{
    NameSpaceObjectList *list;
    NameSpaceList *scan;
    ScopeRec *lookup;
    Type *best;
    NameSpace *bestnspace;

    if (scope->theclass != NULL) {
        CDecl_CompleteType((Type *)scope->theclass);
        if (find_and_append_class_member_path(out, scope, name, 1))
            return 1;
        return 0;
    }

    if ((list = CScope_FindQualName(scope, name)) != NULL) {
        for (; list != NULL; list = list->next) {
            if (list->object->otype == OT_TYPETAG) {
                out->type.base = ((ObjType *)list->object)->type;
                return 1;
            }
        }
    }

    if (scope->usings != NULL) {
        best = NULL;
        for (lookup = build_usings_scope_list(scope); lookup != NULL; lookup = lookup->outer) {
            for (scan = lookup->list; scan != NULL; scan = scan->next) {
                for (list = CScope_FindQualName(scan->nspace, name); list != NULL; list = list->next) {
                    if (list->object->otype == OT_TYPETAG) {
                        if (best != NULL && best != ((ObjType *)list->object)->type)
                            AmbiguousError(scan->nspace, bestnspace, name);
                        best = ((ObjType *)list->object)->type;
                        bestnspace = scan->nspace;
                        break;
                    }
                }
            }
            if (best != NULL) {
                out->type.base = best;
                return 1;
            }
        }
    }
    return 0;
}
#undef CERROR_FILE

NameSpaceObjectList *find_namespace_object(CScopeParseResult *state, NameSpace *nspace, HashNameNode *name,
                                           NameSpace **foundSpace)
{
    NameSpaceObjectList *result;
    ScopeRec *usingSpace;
    NameSpaceObjectList *usingResult;
    NameSpaceObjectList *classResult;
    if (nspace->theclass != NULL) {
        CDecl_CompleteType((Type *)nspace->theclass);
        if (find_and_append_class_member_path(state, nspace, name, 0) != 0) {
            classResult = state->objects;
            state->objects = NULL;
            return classResult;
        }
        return NULL;
    }
    if ((result = CScope_FindQualName(nspace, name)) != NULL) {
        *foundSpace = nspace;
        return result;
    }
    if (nspace->usings != NULL) {
        usingSpace = build_usings_scope_list(nspace);
        while (usingSpace != NULL) {
            usingResult = CScope_0049a000(usingSpace, name, foundSpace);
            if (usingResult != NULL) {
                return usingResult;
            }
            usingSpace = usingSpace->outer;
        }
    }
    return NULL;
}

NameSpace *find_name_nspace(CScopeParseResult *result, NameSpace *nspace, HashNameNode *name)
{
    Boolean local;
    NameSpace *found;
    NameSpaceList *using;
    ScopeRec *scope;
    NameSpaceObjectList *list;

    local = 0;
    if (nspace->theclass != NULL) {
        if (find_and_append_class_member_path(result, nspace, name, 2) != 0) {
            nspace = result->nspace;
            result->nspace = NULL;
            return nspace;
        }
        return NULL;
    }

    if ((list = CScope_FindQualName(nspace, name)) != NULL &&
        get_object_list_nspace((ObjectList *)list, &local) != NULL)
        return nspace;

    if (local == 0 && nspace->usings != NULL) {
        found = NULL;
        for (scope = build_usings_scope_list(nspace); scope != NULL; scope = scope->outer) {
            for (using = scope->list; using != NULL; using = using->next) {
                nspace = using->nspace;
                if ((list = CScope_FindQualName(nspace, name)) != NULL &&
                    (nspace = get_object_list_nspace((ObjectList *)list, &local)) != NULL) {
                    if (found != NULL && nspace != found)
                        CError_ReportError(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
                    found = nspace;
                }
                if (local != 0)
                    return NULL;
            }
            if (found != NULL)
                return found;
        }
    }
    return NULL;
}

Boolean set_parse_result_from_objects(CScopeParseResult *result, NameSpaceObjectList *objects, HashNameNode *name)
{
    if (objects->next == NULL || objects->next->object->otype == OT_TYPETAG) {
        switch (objects->object->otype) {
            case OT_NAMESPACE:
                CError_ReportError(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                return 0;
            case OT_TYPE:
                result->type.base = ((ObjType *)objects->object)->type;
                result->qualifiers = ((ObjType *)objects->object)->qual;
                result->object = objects->object;
                result->name = name;
                result->is_type = 1;
                break;
            case OT_TYPETAG:
                result->type.base = ((ObjType *)objects->object)->type;
                result->qualifiers = 0;
                result->object = objects->object;
                result->name = name;
                result->is_type = 1;
                break;
            default:
                result->object = objects->object;
        }
    } else {
        result->objects = objects;
    }
    return 1;
}
#undef CERROR_FILE

NameSpaceObjectList *find_scope_object_list(LookupCtx *iterator, HashNameNode *name)
{
    NameSpaceObjectList *list;

    if (iterator->scopeChain) {
        if (iterator->scopeChain->list)
            return CScope_0049a000(iterator->scopeChain, name, NULL);
        if (iterator->scopeChain->ns->theclass) {
            if (find_and_append_class_member_path(iterator->lookupState, iterator->scopeChain->ns, name, 0)) {
                list = iterator->lookupState->objects;
                iterator->lookupState->objects = NULL;
                return list;
            } else {
                return NULL;
            }
        }

        if ((list = CScope_FindName(iterator->scopeChain->ns, name)))
            return list;
    } else {
        if (iterator->namespaceCursor->theclass) {
            if (find_and_append_class_member_path(iterator->lookupState, iterator->namespaceCursor, name, 0)) {
                list = iterator->lookupState->objects;
                iterator->lookupState->objects = NULL;
                return list;
            } else {
                return NULL;
            }
        }

        if ((list = CScope_FindName(iterator->namespaceCursor, name)))
            return list;
    }

    return NULL;
}

NameSpaceObjectList *CScope_0049a000(ScopeRec *context, HashNameNode *name, NameSpace **foundnspace)
{
    NameSpaceObjectList *newlist;
    NameSpaceObjectList *scanOL;
    NameSpaceObjectList *existing;
    Boolean copied;
    NameSpace *found;
    NameSpaceList *scan;
    NameSpaceObjectList *candidate;
    NameSpaceObjectList *result;
    NameSpaceObjectList *tmpOL;
#if VERSION >= VERSION_GC_2_7
    NameSpaceName *scanname;
#endif

    if (context->ns) {
#if VERSION >= VERSION_GC_2_7
        for (scanname = context->ns->tparams; scanname; scanname = scanname->next) {
            if (scanname->name == name)
                return &scanname->first;
        }

        if ((tmpOL = CScope_FindName(context->ns, name))) {
#else
        if ((tmpOL = CScope_FindQualName(context->ns, name))) {
#endif
            result = tmpOL;
            found = context->ns;
        } else {
            result = NULL;
            found = NULL;
        }
    } else {
        result = NULL;
        found = NULL;
    }

    copied = 0;
    for (scan = context->list; scan; scan = scan->next) {
#if VERSION >= VERSION_GC_2_7
        if ((tmpOL = CScope_FindName(scan->nspace, name))) {
#else
        if ((tmpOL = CScope_FindQualName(scan->nspace, name))) {
#endif
            if (result) {
                for (candidate = tmpOL; candidate; candidate = candidate->next) {
                    for (existing = result; existing; existing = existing->next) {
                        if (existing->object == candidate->object)
                            break;
                        if (existing->object->otype == candidate->object->otype) {
                            switch (existing->object->otype) {
                                case OT_TYPE:
                                    if ((((ObjType *)existing->object)->type == ((ObjType *)candidate->object)->type) &&
                                        (((ObjType *)existing->object)->qual == ((ObjType *)candidate->object)->qual))
                                        goto break1;
                                    break;
                                case OT_TYPETAG:
                                    if (((ObjType *)existing->object)->type == ((ObjType *)candidate->object)->type)
                                        goto break1;
                                    break;
                                case OT_ENUMCONST:
                                    if (((ObjEnumConst *)existing->object)->type ==
                                        ((ObjEnumConst *)candidate->object)->type)
                                        goto break1;
                                    break;
                                case OT_OBJECT:
#if VERSION < VERSION_GC_2_7
                                    if (((Object *)existing->object)->type->type == TYPEFUNC)
                                        continue;
                                    if (((Object *)candidate->object)->type->type == TYPEFUNC)
                                        continue;
#endif
                                    if (((Object *)existing->object)->datatype == DALIAS) {
                                        if (((Object *)candidate->object)->datatype == DALIAS) {
                                            if (((Object *)existing->object)->u.alias.object ==
                                                ((Object *)candidate->object)->u.alias.object)
                                                goto break1;
                                        } else {
                                            if (((Object *)existing->object)->u.alias.object ==
                                                (Object *)candidate->object)
                                                goto break1;
                                        }
                                    } else {
                                        if ((((Object *)candidate->object)->datatype == DALIAS) &&
                                            (Object *)existing->object == ((Object *)candidate->object)->u.alias.object)
                                            goto break1;
                                    }
                                    break;
                            }
                        }
                    }
                break1:
#if VERSION >= VERSION_GC_2_7
                    if (!existing) {
                        if (!copied) {
                            result = CScope_CopyList(result);
                            copied = 1;
                        }
                        if (CScope_ObjectIsFunction((Object *)candidate->object)) {
                            for (scanOL = result; scanOL; scanOL = scanOL->next) {
                                if (!CScope_ObjectIsFunction((Object *)scanOL->object)) {
                                    if (scanOL->object->otype != OT_TYPETAG) {
                                        AmbiguousError(found, scan->nspace, name);
                                        break;
                                    }
                                }
                            }
                        } else if (candidate->object->otype == OT_TYPETAG) {
                            for (scanOL = result; scanOL; scanOL = scanOL->next) {
                                if (scanOL->object->otype == OT_TYPETAG) {
                                    AmbiguousError(found, scan->nspace, name);
                                    break;
                                }
                            }
                        } else {
                            for (scanOL = result; scanOL; scanOL = scanOL->next) {
                                if (scanOL->object->otype != OT_TYPETAG) {
                                    AmbiguousError(found, scan->nspace, name);
                                    break;
                                }
                            }
                        }

                        if (!scanOL) {
                            newlist = CompilerTools_AllocatePool(sizeof(NameSpaceObjectList));
                            newlist->object = candidate->object;
                            newlist->next = result;
                            result = newlist;
                        }
                    }
#else
                    if (existing)
                        continue;
                    if (!copied) {
                        NameSpaceObjectList *entry;

                        for (entry = result; entry; entry = entry->next) {
                            if (CScope_ObjectIsFunction((Object *)entry->object))
                                continue;
                            AmbiguousError(found, scan->nspace, name);
                        }
                        result = CScope_CopyList(result);
                        copied = 1;
                    }
                    if (CScope_ObjectIsFunction((Object *)candidate->object)) {
                        newlist = CompilerTools_AllocatePool(sizeof(NameSpaceObjectList));
                        newlist->object = candidate->object;
                        newlist->next = result;
                        result = newlist;
                    } else {
                        AmbiguousError(found, scan->nspace, name);
                    }
#endif
                }
            } else {
                result = tmpOL;
                found = scan->nspace;
            }
        }
    }

    if (foundnspace)
        *foundnspace = found;
    return result;
}

/* The path to the class (TCLASS or one of its bases, OFFSET into the object) that declares the member searched for
   (class_member_name, looked up as data_00580dec says); an ambiguous match is reported. */
BClassList *find_class_member_path(CScopeParseResult *result, TypeClass *tclass, SInt32 offset)
{
    BClassList *newlist;
    SInt32 thisoffset;
    BClassList *n;
    BClassList *candidate;
    ClassList *base;
    BClassList *bestBase;
    TypeClass *bestClass;
    NameSpaceObjectList *list;
    NameSpace *nspace;
    Boolean fail;

    if ((list = CScope_FindName(tclass->nspace, class_member_name))) {
        if (found_class) {
            if (CClass_ClassDominates(found_class, tclass))
                return NULL;
            if (CClass_ClassDominates(tclass, found_class))
                found_class = NULL;
        }

        switch (data_00580dec) {
            case 2:
                fail = 0;
                if ((nspace = get_object_list_nspace((ObjectList *)list, &fail))) {
                    if (found_class) {
                        if (found_class != tclass) {
                            AmbiguousError(found_class->nspace, tclass->nspace, class_member_name);
                            return NULL;
                        }
                        if (class_path_offset != offset)
                            data_00580ded = 1;
                        return NULL;
                    }

                    found_class = tclass;
                    class_path_offset = offset;
                    result->nspace = nspace;

                    newlist = CompilerTools_AllocatePool(sizeof(BClassList));
                    newlist->next = NULL;
                    newlist->type = (Type *)tclass;
                    return newlist;
                }

                if (fail)
                    return NULL;
                break;
            case 0:
                if (found_class) {
                    if (list->object->otype == OT_TYPETAG && result->objects->object->otype == OT_TYPETAG &&
                        ((ObjType *)list->object)->type->type == TYPECLASS &&
                        ((ObjType *)result->objects->object)->type->type == TYPECLASS &&
                        (((TypeClass *)((ObjType *)list->object)->type)->flags & CLASS_IS_TEMPL_INST) &&
                        (((TypeClass *)((ObjType *)result->objects->object)->type)->flags & CLASS_IS_TEMPL_INST) &&
                        ((TypeClassExt800 *)((ObjType *)list->object)->type)->classTemplate ==
                            ((TypeClassExt800 *)((ObjType *)result->objects->object)->type)->classTemplate) {
                        data_00580de4 = ((TypeClassExt800 *)((ObjType *)result->objects->object)->type)->classTemplate;
                    } else {
                        if (found_class != tclass) {
                            AmbiguousError(found_class->nspace, tclass->nspace, class_member_name);
                            return NULL;
                        }
                        if (class_path_offset != offset) {
                            data_00580ded = 1;
                            return NULL;
                        }
                    }
                }

                found_class = tclass;
                class_path_offset = offset;
                result->objects = list;
                newlist = CompilerTools_AllocatePool(sizeof(BClassList));
                newlist->next = NULL;
                newlist->type = (Type *)tclass;
                return newlist;
            case 1:
                for (; list; list = list->next) {
                    if (list->object->otype == OT_TYPETAG) {
                        if (found_class) {
                            if (found_class != tclass) {
                                AmbiguousError(found_class->nspace, tclass->nspace, class_member_name);
                                return NULL;
                            }
                            if (class_path_offset != offset)
                                data_00580ded = 1;
                            return NULL;
                        }

                        found_class = tclass;
                        class_path_offset = offset;
                        result->type.base = ((ObjType *)list->object)->type;
                        newlist = CompilerTools_AllocatePool(sizeof(BClassList));
                        newlist->next = NULL;
                        newlist->type = (Type *)tclass;
                        return newlist;
                    }
                }
                break;
            default:
#if VERSION >= VERSION_GC_2_7
                CError_FATAL(1202);
#else
                CError_FATAL(1182);
#endif
        }
    }

    for (base = tclass->bases, bestBase = NULL; base; base = base->next) {
        thisoffset = base->is_virtual ? CClass_FindVBaseOffset(class_path_base, base->base) : offset + base->offset;
        if ((candidate = find_class_member_path(result, base->base, thisoffset))) {
            n = CompilerTools_AllocatePool(sizeof(BClassList));
            n->next = candidate;
            n->type = (Type *)tclass;
            if (bestBase && bestClass == found_class) {
                if (CClass_IsMoreAccessiblePath(n, bestBase))
                    bestBase = n;
            } else {
                bestClass = found_class;
                bestBase = n;
            }
        }
    }

    return bestBase;
}

Boolean find_and_append_class_member_path(CScopeParseResult *scope, NameSpace *target, HashNameNode *mode, Boolean flag)
{
    BClassList *node;
    BClassList *list;

    class_path_base = target->theclass;
    found_class = NULL;
    data_00580de4 = NULL;
    class_member_name = mode;
    data_00580dec = flag;
    data_00580ded = 0;
    node = find_class_member_path(scope, class_path_base, 0);
    if (node != NULL) {
        if (data_00580de4 != NULL)
            return 1;
        if (scope->basePath != NULL) {
            list = scope->basePath;
            while (list->next != NULL)
                list = list->next;
            if ((TypeClass *)list->type != class_path_base)
                list->next = node;
            else {
                node = node->next;
                list->next = node;
            }
        } else {
            scope->basePath = node;
        }
        if (data_00580ded != 0)
            scope->isambig = 1;
        return 1;
    }
    return 0;
}
#undef CERROR_FILE

NameSpace *get_object_list_nspace(ObjectList *objects, Boolean *flag)
{
    Type *type;
    for (;;) {
        if (objects == NULL)
            return NULL;
        switch (objects->object.value->otype) {
            case OT_TYPE:
                type = ((ObjType *)objects->object.value)->type;
                break;
            case OT_TYPETAG:
                type = ((ObjType *)objects->object.value)->type;
                break;
            case OT_NAMESPACE:
                return ((ObjNameSpace *)objects->object.value)->nspace;
            default:
#if VERSION >= VERSION_GC_2_7
                CError_FATAL(1052);
#else
                CError_FATAL(1032);
#endif
                break;
            case OT_ENUMCONST:
            case OT_MEMBERVAR:
            case OT_OBJECT:
                objects = objects->next;
                continue;
        }
        if (type->type != TYPECLASS) {
            if (type->type == TYPETEMPLATE)
                return NULL;
            CError_ReportError(ERR_ILLEGAL_NAMESPACE);
            if (flag != NULL)
                *flag = 1;
            return NULL;
        }
        return TYPE_CLASS(type)->nspace;
    }
}
#undef CERROR_FILE

static inline ScopeRec *CScope_FindUsingScope(ScopeRec *scope, NameSpaceList *used)
{
    NameSpace *ancestor;
    for (; scope; scope = scope->outer) {
        for (ancestor = used->nspace; ancestor; ancestor = ancestor->parent)
            if (scope->ns == ancestor)
                return scope;
    }
#if VERSION >= VERSION_GC_2_7
    CError_FATAL(853);
#else
    CError_FATAL(835);
#endif
    return scope;
}

static Boolean ScSeen(ScopeRec *base, NameSpaceList *item)
{
    NameSpace *ns = item->nspace;
    ScopeRec *n;
    NameSpaceList *it;

    for (n = base; n != NULL; n = n->outer) {
        if (n->ns == ns)
            return 1;
        for (it = n->list; it != NULL; it = it->next)
            if (it->nspace == ns)
                return 1;
    }
    return 0;
}

static void ScAdd(ScopeRec *node, NameSpaceList *item)
{
    NameSpaceList *it;

    it = CompilerTools_AllocatePool(sizeof(NameSpaceList));
    it->nspace = item->nspace;
    it->next = node->outer->list;
    node->outer->list = it;
}

ScopeRec *build_usings_scope_list(NameSpace *ns)
{
    ScopeRec *scope;
    ScopeRec root;
    NameSpaceList *namespaceEntry;
    NameSpaceList *usingEntry;

    root.ns = ns;
    root.list = NULL;
    root.outer = NULL;
    for (scope = &root; scope != NULL; scope = scope->outer) {
        if (scope->ns != NULL) {
            for (usingEntry = scope->ns->usings; usingEntry != NULL; usingEntry = usingEntry->next) {
                if (!ScSeen(&root, usingEntry)) {
                    if (scope->outer == NULL) {
                        scope->outer = CompilerTools_AllocatePool(sizeof(ScopeRec));
                        scope->outer->ns = NULL;
                        scope->outer->list = NULL;
                        scope->outer->outer = NULL;
                    }
                    ScAdd(scope, usingEntry);
                }
            }
        }
        for (namespaceEntry = scope->list; namespaceEntry != NULL; namespaceEntry = namespaceEntry->next) {
            for (usingEntry = namespaceEntry->nspace->usings; usingEntry != NULL; usingEntry = usingEntry->next) {
                if (!ScSeen(&root, usingEntry)) {
                    if (scope->outer == NULL) {
                        scope->outer = CompilerTools_AllocatePool(sizeof(ScopeRec));
                        scope->outer->ns = NULL;
                        scope->outer->list = NULL;
                        scope->outer->outer = NULL;
                    }
                    ScAdd(scope, usingEntry);
                }
            }
        }
    }
    return root.outer;
}

struct ScopeRec *build_namespace_scope_rec(NameSpace *nspace)
{
    ScopeRec *rec;
    NameSpaceList head;
    NameSpaceList *scope;
    NameSpaceList *u;
    NameSpaceList *p;
    NameSpaceList *used;
    ScopeRec *r;

    rec = CompilerTools_AllocatePool(12);
    memclrw(rec, 12);
    rec->ns = nspace;
    if (nspace->parent)
        rec->outer = build_namespace_scope_rec(nspace->parent);
    if (nspace->usings) {
        head.nspace = nspace;
        head.next = NULL;
        for (scope = &head; scope; scope = scope->next) {
            for (u = scope->nspace->usings; u; u = u->next) {
                for (p = &head; p; p = p->next)
                    if (p->nspace == u->nspace)
                        break;
                if (!p) {
                    p = CompilerTools_AllocatePool(8);
                    p->nspace = u->nspace;
                    p->next = scope->next;
                    scope->next = p;
                }
            }
        }
        for (used = head.next; used; used = used->next) {
            r = CScope_FindUsingScope(rec, used);
            for (p = r->list; p; p = p->next)
                if (p->nspace == used->nspace)
                    break;
            if (!p) {
                p = CompilerTools_AllocatePool(8);
                p->nspace = used->nspace;
                p->next = r->list;
                r->list = p;
            }
        }
    }
    return rec;
}
#undef CERROR_FILE

void CScope_AddGlobalObject(Object *object)
{
    object->nspace = registration_context;
    CScope_AddObject(registration_context, object->name, (ObjBase *)object);
}

void CScope_AddObject(NameSpace *scope, HashNameNode *name, ObjBase *object)
{
    HashNameNode *lookupName;
    char kind;
    NameSpaceName **slot;
    NameSpaceObjectList *current;
    NameSpaceName *entry;
    NameSpaceObjectList *first;
    NameSpaceObjectList *newItem;
    Boolean isFunction;
    Boolean isExistingFunction;
    NameSpaceName *newEntry;

    if (!scope->is_hash)
        entry = scope->data.list;
    else
        entry = scope->data.hash[name->hashval & 1023];
    goto search;
nextEntry:
    if (entry->name != (lookupName = name))
        goto advance;
    first = &entry->first;
    goto found;
advance:
    entry = entry->next;
search:
    if (entry)
        goto nextEntry;
    first = NULL;
found:;
    if ((current = first) == NULL)
        goto addName;
    {
        newItem = scope->is_global ? (NameSpaceObjectList *)galloc(sizeof(NameSpaceObjectList))
                                   : (NameSpaceObjectList *)CompilerTools_AllocatePool(sizeof(NameSpaceObjectList));
        if ((kind = object->otype) == 3 || first->object->otype == OT_NAMESPACE) {
            CError_ReportError(ERR_ILLEGAL_NAME_OVERLOADING);
            return;
        }
        if (kind == 2) {
            for (;;) {
                if (current->object->otype == OT_TYPETAG) {
                    CError_ReportError(ERR_ILLEGAL_NAME_OVERLOADING);
                    return;
                }
                if (copts.cplusplus && current->object->otype == OT_TYPE &&
                    !iscpp_typeequal(((ObjType *)object)->type, ((ObjType *)current->object)->type)) {
                    CError_ReportError(ERR_TYPENAME_REDEFINED);
                    return;
                }
                if (!current->next) {
                    current->next = newItem;
                    newItem->next = NULL;
                    newItem->object = object;
                    return;
                }
                current = current->next;
            }
        }
        if (first->object->otype == OT_TYPETAG) {
            if (copts.cplusplus && kind == 1 &&
                !iscpp_typeequal(((ObjType *)object)->type, ((ObjType *)first->object)->type)) {
                CError_ReportError(ERR_TYPENAME_REDEFINED);
                return;
            }
#if VERSION >= VERSION_GC_2_7
            if (first->next)
                CError_FATAL(739);
#else
            if (first->next)
                CError_FATAL(721);
#endif
            newItem->object = first->object;
            newItem->next = NULL;
            first->object = object;
            first->next = newItem;
            goto done;
        }
        if (!copts.cplusplus)
            goto illegalOverload;
        isFunction = kind == 5 && ((Object *)object)->type->type == TYPEFUNC;
        if (isFunction)
            goto functions;
    illegalOverload:
        CError_ReportError(ERR_ILLEGAL_NAME_OVERLOADING);
        return;
    functions:
#if VERSION >= VERSION_GC_2_7
        if (current->object->otype != OT_TYPETAG)
#else
        if ((kind = current->object->otype) != 2)
#endif
            goto checkFunction;
        current->next = (NameSpaceObjectList *)galloc(sizeof(NameSpaceObjectList));
        current->next->next = NULL;
        current->next->object = current->object;
        current->object = object;
        return;
    checkFunction:
#if VERSION >= VERSION_GC_2_7
        isExistingFunction = current->object->otype == OT_OBJECT && ((Object *)current->object)->type->type == TYPEFUNC;
#else
        isExistingFunction = kind == 5 && ((Object *)current->object)->type->type == TYPEFUNC;
#endif
        if (isExistingFunction)
            goto appendFunction;
        CError_ReportError(ERR_ILLEGAL_NAME_OVERLOADING);
        return;
    appendFunction:
        if (current->next != NULL)
            goto nextFunction;
        current->next = (NameSpaceObjectList *)galloc(sizeof(NameSpaceObjectList));
        current->next->next = NULL;
        current->next->object = object;
        return;
    nextFunction:
        current = current->next;
        goto functions;
    }
addName:
    if (scope->theclass && (scope->theclass->flags & (CLASS_IS_TEMPL | CLASS_IS_TEMPL_INST))) {
        CScope_InsertName(scope, name)->object = object;
    } else {
        if (scope->is_global)
            newEntry = (NameSpaceName *)galloc(sizeof(NameSpaceName));
        else
            newEntry = (NameSpaceName *)CompilerTools_AllocatePool(sizeof(NameSpaceName));
        newEntry->name = name;
        newEntry->first.next = NULL;
        newEntry->first.object = NULL;
        if (scope->is_hash) {
            slot = &scope->data.hash[name->hashval & 1023];
            newEntry->next = *slot;
            *slot = newEntry;
        } else {
            newEntry->next = scope->data.list;
            scope->data.list = newEntry;
        }
        scope->names += 1;
        newEntry->first.object = object;
    }
done:
    return;
}

NameSpace *CScope_NewListNameSpace(HashNameNode *name, Boolean is_global)
{
    NameSpace *ns;

    if (is_global) {
        ns = (NameSpace *)galloc(sizeof(NameSpace));
        memclrw(ns, sizeof(NameSpace));
    } else {
        ns = (NameSpace *)CompilerTools_AllocatePool(sizeof(NameSpace));
        memclrw(ns, sizeof(NameSpace));
    }
    ns->name = name;
    ns->is_hash = 0;
    ns->is_global = is_global;
    return ns;
}

static inline NameSpaceList *CScope_BuildAssociatedNameSpaceList(ENodeList *enodeList)
{
    NameSpaceList *nslist = NULL;

    while (enodeList) {
        nslist = collect_type_namespaces(nslist, enodeList->node->rtype);
        enodeList = enodeList->next;
    }
    return nslist;
}

static inline Boolean CScope_IsSameObject(Object *a, Object *b)
{
    if (a == b)
        return 1;
    return (a->nspace == b->nspace) && (a->name == b->name) && iscpp_typeequal(a->type, b->type);
}

NameSpaceObjectList *CScope_ArgumentDependentNameLookup(NameSpaceObjectList *list, HashNameNode *name,
                                                        ENodeList *argNodes, char flag)
{
    NameSpaceList *scan;
    NameSpaceObjectList *iscan;
    NameSpaceObjectList *iiscan;
    NameSpaceObjectList *newlist;

    for (scan = CScope_BuildAssociatedNameSpaceList(argNodes); scan; scan = scan->next) {
        for (iscan = CScope_FindQualName(scan->nspace, name); iscan; iscan = iscan->next) {
            if (iscan->object->otype != OT_OBJECT)
                continue;
            if (((Object *)iscan->object)->type->type != TYPEFUNC)
                continue;
            if (flag && (((TypeFunc *)((Object *)iscan->object)->type)->flags & FUNC_METHOD))
                continue;

            for (iiscan = list; iiscan; iiscan = iiscan->next) {
                if (iiscan->object->otype == OT_OBJECT &&
                    CScope_IsSameObject((Object *)iiscan->object, (Object *)iscan->object))
                    break;
            }

            if (!iiscan) {
                newlist = CompilerTools_AllocatePool(sizeof(NameSpaceObjectList));
                newlist->object = iscan->object;
                newlist->next = list;
                list = newlist;
            }
        }
    }

    return list;
}

NameSpaceList *collect_type_namespaces(NameSpaceList *acc, Type *type)
{
    FuncArg *arg;

    for (;;) {
        switch ((SInt8)type->type) {
            case TYPEPOINTER:
            case TYPEARRAY:
                type = ((TypePointer *)type)->target;
                continue;
            case TYPEENUM:
                acc = fn_0049b300(acc, ((TypeEnum *)type)->nspace);
                break;
            case TYPEFUNC:
                for (arg = ((TypeFunc *)type)->args; arg != NULL; arg = arg->next) {
                    if (arg->type != NULL)
                        acc = collect_type_namespaces(acc, arg->type);
                }
                type = ((TypeFunc *)type)->functype;
                continue;
            case TYPEMEMBERPOINTER:
                acc = collect_type_namespaces(acc, ((TypeMemberPointer *)type)->memberType);
                type = ((TypeMemberPointer *)type)->owner.type;
                if (type->type != TYPECLASS)
                    break;
                /* fall through */
            case TYPECLASS:
                acc = fn_0049b300(acc, ((TypeClass *)type)->nspace);
                break;
            case TYPEVOID:
            case TYPEINT:
            case TYPEFLOAT:
            case TYPESTRUCT:
            case TYPEBITFIELD:
            case TYPETEMPLATE:
            case TYPETEMPLDEPEXPR:
                break;
            default:
#if VERSION >= VERSION_GC_2_7
                CError_FATAL(494);
#else
                CError_FATAL(476);
#endif
                break;
        }
        break;
    }
    return acc;
}
#undef CERROR_FILE

NameSpaceObjectList *CScope_InsertNameSpaceName(NameSpace *nspace, HashNameNode *name)
{
    NameSpaceName *entry;
    if (nspace->is_global)
        entry = (NameSpaceName *)galloc(sizeof(NameSpaceName));
    else
        entry = (NameSpaceName *)CompilerTools_AllocatePool(sizeof(NameSpaceName));
    entry->name = name;
    entry->first.next = NULL;
    entry->first.object = NULL;
    if (nspace->is_hash) {
        NameSpaceName **bucket = &nspace->data.hash[name->hashval & 1023];
        entry->next = *bucket;
        *bucket = entry;
    } else {
        entry->next = nspace->data.list;
        nspace->data.list = entry;
    }
    nspace->names++;
    return &entry->first;
}

UInt8 CScope_IsEmptyNameSpace(NameSpace *nameSpace)
{
    if (nameSpace->is_hash != '\0') {
#if VERSION >= VERSION_GC_2_7
        CError_FATAL(664);
#else
        CError_FATAL(646);
#endif
    }
    return nameSpace->data.list == NULL;
}

NameSpace *CScope_FindGlobalNS(NameSpace *scope)

{
    while (scope != NULL) {
        if (((scope->name != NULL) && (scope->theclass == NULL)) && (scope->is_global != 0)) {
            return scope;
        }
        scope = scope->parent;
    }
    return registration_context;
}

NameSpace *CScope_FindNonClassNonTemplNameSpace(NameSpace *nspace)
{
    while (nspace != NULL) {
        if (nspace->theclass == NULL && nspace->is_templ == '\0') {
            return nspace;
        }
        nspace = nspace->parent;
    }
    return registration_context;
}

UInt8 CScope_IsInLocalNameSpace(NameSpace *scope)
{
    if (scope != NULL) {
        do {
            if (!scope->is_global && !scope->is_templ) {
                return 1;
            }
            scope = scope->parent;
        } while (scope != NULL);
    }
    return 0;
}

Boolean CScope_FindClassMemberObject(TypeClass *tclass, CScopeParseResult *result, HashNameNode *name)
{
    NameSpaceObjectList *objects;
    Boolean success;

    memclrw(result, sizeof(*result));
    success = find_and_append_class_member_path(result, tclass->nspace, name, 0);
    if (success) {
        objects = result->objects;
        result->objects = NULL;
        if (objects != NULL && objects->object->otype == OT_OBJECT) {
            success = set_parse_result_from_objects(result, objects, name);
            return success;
        }
    }
    return 0;
}

UInt8 CScope_FindQualifiedClassMember(CScopeParseResult *holder, TypeClass *type, HashNameNode *name)
{
    Boolean success;
    NameSpaceObjectList *objects;

    memclrw(holder, sizeof(*holder));
    CDecl_CompleteType((Type *)type);
    success = find_and_append_class_member_path(holder, type->nspace, name, 0);
    if (success) {
        if ((objects = holder->objects) == NULL) {
#if VERSION >= VERSION_GC_2_7
            CError_FATAL(1780);
#else
            CError_FATAL(1723);
#endif
        }
        holder->objects = NULL;
        success = set_parse_result_from_objects(holder, objects, name);
        if (success && holder->type.base == NULL) {
            return 1;
        }
        CError_ReportError(ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE, name->name);
    }
    return 0;
}

BClassList *find_base_class_path(TypeClass *theclass, TypeClass *target, unsigned int offset)
{
    BClassList *list;
    ClassList *base;
    BClassList *best;

    if (theclass == target) {
        if (found_class && class_path_offset != offset)
            CError_ReportError(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);

        list = CompilerTools_AllocatePool(sizeof(BClassList));
        list->next = NULL;
        list->type = (Type *)theclass;
        found_class = theclass;
        class_path_offset = offset;
        return list;
    }

#if VERSION >= VERSION_GC_2_7
    for (base = theclass->bases, best = NULL; base; base = base->next) {
#else
    best = NULL;
    for (base = theclass->bases; base; base = base->next) {
#endif
        if ((list = find_base_class_path(base->base, target,
                                         base->is_virtual ? CClass_FindVBaseOffset(class_path_base, base->base)
                                                          : (offset + base->offset))))
            best = list;
    }

    if (best) {
        list = CompilerTools_AllocatePool(sizeof(BClassList));
        list->next = best;
        list->type = (Type *)theclass;
        return list;
    }

    return NULL;
}

void CScope_MergeNameSpace(NameSpace *dest, NameSpace *source)
{
    NameSpaceName *last;
    if (dest->is_hash != 0 || source->is_hash != 0) {
#if VERSION >= VERSION_GC_2_7
        CError_FATAL(678);
#else
        CError_FATAL(660);
#endif
    }
    if (dest->data.list != NULL) {
        last = dest->data.list;
        while (last->next != NULL)
            last = last->next;
        last->next = source->data.list;
    } else {
        dest->data.list = source->data.list;
    }
}

NameSpace *CScope_NewHashNameSpace(HashNameNode *name)
{
    NameSpace *nspace;
    NameSpaceName **hash;
    hash = (NameSpaceName **)galloc(4096U);
    memclrw(hash, 4096U);
    nspace = (NameSpace *)galloc(sizeof(NameSpace));
    memclrw(nspace, sizeof(NameSpace));
    nspace->name = name;
    nspace->data.hash = hash;
    nspace->is_hash = 1;
    nspace->is_global = 1;
    return nspace;
}

NameSpaceList *fn_0049b300(NameSpaceList *list, NameSpace *nspace)
{
    NameSpaceList *n;
    ClassList *e;

    for (n = list; n != NULL; n = n->next) {
        if (n->nspace == nspace)
            return list;
    }

    n = (NameSpaceList *)CompilerTools_AllocatePool(8);
    n->next = list;
    n->nspace = nspace;
    list = n;

    if (nspace->theclass != NULL) {
        list = fn_0049b300(list, nspace->parent);
        for (e = nspace->theclass->bases; e != NULL; e = e->next) {
            list = fn_0049b300(list, e->base->nspace);
        }
    }
    return list;
}

NameSpaceObjectList *CScope_InsertName(NameSpace *scope, HashNameNode *name)
{
    NameSpaceName *entry;
    NameSpace *target;
    NameSpaceName *tail;
    target = scope;
    if (target->is_hash != 0) {
#if VERSION >= VERSION_GC_2_7
        CError_FATAL(387);
#else
        CError_FATAL(369);
#endif
    }
    if (target->is_global != 0) {
        entry = (NameSpaceName *)galloc(sizeof(NameSpaceName));
    } else {
        entry = (NameSpaceName *)CompilerTools_AllocatePool(sizeof(NameSpaceName));
    }
    entry->next = NULL;
    entry->name = name;
    entry->first.next = NULL;
    entry->first.object = NULL;
    if (target->data.list != NULL) {
        tail = target->data.list;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = entry;
    } else {
        target->data.list = entry;
    }
    target->names += 1U;
    return &entry->first;
}

NameSpaceName *CScope_FindNameSpaceName(NameSpace *nspace, HashNameNode *name)
{
    NameSpaceName *scan;

#if VERSION >= VERSION_GC_2_7
    for (scan = nspace->tparams; scan; scan = scan->next) {
        if (scan->name == name)
            return scan;
    }
#endif

    if (!nspace->is_hash)
        scan = nspace->data.list;
    else
        scan = nspace->data.hash[name->hashval & 0x3ff];

    for (; scan; scan = scan->next) {
        if (scan->name == name)
            return scan;
    }

    return NULL;
}

Boolean CScope_IsEmptySymTable(void)
{
    SInt32 i;
    NameSpaceObjectList *ol;
    NameSpaceName *nsn;

    if (!registration_context->is_hash)
        CError_FATAL(232);

    for (i = 0; i < 0x400; i++) {
        for (nsn = registration_context->data.hash[i]; nsn != NULL; nsn = nsn->next) {
            for (ol = &nsn->first; ol != NULL; ol = ol->next) {
                if (ol->object->otype != OT_OBJECT || !CParser_IsPublicRuntimeObject(ol->object))
                    return 0;
            }
        }
    }
    return 1;
}

/* Enters member function FUNCTION of THECLASS (static when IS_STATIC), saving the current scope in SAVE. */
void CScope_SetMethodScope(Object *cls, TypeClass *ns, unsigned char flag, CScopeSave *save)
{
    save->nspace = currentNameSpace;
    save->theclass = data_00588040;
    save->function = data_00588238;
    save->member_context = data_005884f8;
    data_00588238 = cls;
    data_00588040 = ns;
    currentNameSpace = ns->nspace;
    data_005884f8 = !flag;
}

/* Enters FUNCTION's scope, saving the current one in SAVED. */
void CScope_SetFunctionScope(Object *function, CScopeSave *saved)
{
    saved->nspace = currentNameSpace;
    saved->theclass = data_00588040;
    saved->function = data_00588238;
    saved->member_context = data_005884f8;
    data_00588238 = function;
    data_00588040 = NULL;
    data_005884f8 = FALSE;
    if ((((TypeMemberFunc *)function->type)->flags & FUNC_METHOD) != 0) {
        data_00588040 = ((TypeMemberFunc *)function->type)->theclass;
        currentNameSpace = data_00588040->nspace;
        data_005884f8 = !((TypeMemberFunc *)function->type)->is_static;
    } else {
        currentNameSpace = function->nspace;
    }
}

#undef CERROR_FILE
