#include "compiler/common.h"
#include "driver/CLCompilerLinkerDropin_V10.h"
#include "compiler/CPrep.h"
#include "driver/Files.h"
#ifndef MWCC_PLUGINS_H
#define MWCC_PLUGINS_H

/* The driver's plugin objects (C++: CLDropinCallbacks_V10.cpp, CLCompilerLinkerDropin_V10.cpp, CLPluginRequests.cpp).
 * CLPluginRequests creates one per plugin by its type code: 'cldr' a PluginA, 'Pars' a PluginB, 'Comp'/'Link' a
 * PluginC; the class names are ours. */

extern "C" {
}

#pragma options align=mac68k
class PluginA {
public:
    PluginA(UInt32 code, int size);
    /* (user-declared: PluginC's constructor, which calls new after constructing
       its base, keeps `this` in a stack slot) */
    ~PluginA();
    UInt8 pad0[0x9c];
    UInt32 sig;         /* 0x9c */
    UInt32 code;        /* 0xa0 */
    UInt8 pada4[0xc];
    void *fb0;          /* 0xb0 */
    UInt8 padb4[0x50];
    void *f104;         /* 0x104 */
};

class PluginB : public PluginA {
public:
    PluginB();
    UInt32 first;                 /* 0x108 */
    UInt32 second;                /* 0x10c */
    UInt32 third;                 /* 0x110 */
    UInt8 intermediateState[8];   /* 0x114 */
    UInt32 fourth;                /* 0x11c */
    UInt32 fifth;                 /* 0x120 */
    UInt32 sixth;                 /* 0x124 */
    UInt32 seventh;               /* 0x128 */
    UInt32 eighth;                /* 0x12c */
    UInt32 ninth;                 /* 0x130 */
    void *dataReference;          /* 0x134 */
};

struct CWTargetInfoV7 {
    CWFileSpec outfile;
    CWFileSpec symfile;
    SInt16 linkType;
    Boolean canRun;
    Boolean canDebug;
    Boolean useRunHelperApp;
    char reserved1;
    UInt32 debuggerCreator;
    UInt32 runHelperCreator;
    SInt32 reserved2[2];
};
struct CWEnvVarInfo;

class PluginC : public PluginA {
public:
    PluginC();
    SInt32 whichfile;                      /* 0x108 */
    CWFileSpec sourcefile;                 /* 0x10c */
    const char *sourcetext;                /* 0x152 */
    SInt32 sourcetextsize;
    Boolean precompile;                   /* 0x15a */
    Boolean autoprecompile;
    Boolean preprocess;
    Boolean cachingPCH;
    Boolean debuginfo;
    SInt16 fileID;                         /* 0x160 */
    BrowseOptions browseoptions;          /* 0x162 */
    Boolean recordbrowseinfo;
    void *reserved;                       /* 0x174 */
    SInt32 sequenceID;
    PluginC *parentPB;
    void *targetStorage;
    SInt32 reservedcompiler[7];            /* 0x184 */
    StorageHandle *texthandle;             /* 0x1a0 */
    CWTargetInfoV7 targetinfo_V7;  /* 0x1a4 */
    TgtRec *targetinfo;                    /* 0x246 */
    const char *commandLineArgs;
    CWFileSpec *workingDirectorySpec;
    SInt32 numEnvironmentVariables;
    CWEnvVarInfo *environmentVariables;
    SInt32 workingDirectoryError;
    SInt32 reservedlinker[2];
    ObjectCallbackContext *callbacks;     /* 0x266 */
};
#pragma options align=reset

#endif
