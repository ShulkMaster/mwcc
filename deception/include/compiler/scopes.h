#ifndef COMPILER_SCOPES_H
#define COMPILER_SCOPES_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma options align = mac68k
struct NameSpace {
    NameSpace *parent;
    HashNameNode *name;
    NameSpaceList *usings;
    TypeClass *theclass;
    union {
        NameSpaceName **hash;
        NameSpaceName *list;
    } data;
    UInt32 names;
    Boolean is_hash;
    Boolean is_global;
    Boolean is_unnamed;
    Boolean is_templ;
};
#pragma options align = reset
#pragma options align = mac68k
struct NameSpaceList {
    NameSpaceList *next;
    NameSpace *nspace;
};
#pragma options align = reset
#pragma options align = mac68k
struct NameSpaceObjectList {
    NameSpaceObjectList *next; /* 0x00: CScope_AddObject links candidates with the same name */
    ObjBase *object; /* 0x04: find_class_member_path tests otype to select namespace, type or object candidates */
};
#pragma options align = reset
#pragma options align = mac68k
struct NameSpaceName {
    NameSpaceName *next;
    HashNameNode *name;
    NameSpaceObjectList first;
};
#pragma options align = reset
extern ObjectList *arguments;
extern ObjectList *locals;

#ifdef __cplusplus
}
#endif

#endif
