#ifndef DRIVER_TOOLHELPERS_CC_H
#define DRIVER_TOOLHELPERS_CC_H

#include <setjmp.h>
#include "version.h"
#include "compiler/common.h"
#include "driver/CLAccessPaths.h"

#ifdef __cplusplus
extern "C" {
#endif

union RecoveryPathFrame {
    OSPathBuffer copy;
    unsigned char bytes[324];
};
extern jmp_buf plugin_request_jmp_buf;
extern struct CWPluginPrivateContext *pluginPrivateContext;
extern void ToolHelpers_cc_PrintCLanguageWarningOptions(void *self);
extern void ToolHelpers_cc_CallValuePairCallback(char *key, struct StorageHandle *value);
extern char *format_version(unsigned int version, char *buf);
extern void ToolHelpers_cc_PrintVersion(char includeValue);
extern char data_00587e22;
extern char data_0054a888[];
extern char data_0054a870[14], data_0054a880[2], data_0054a884[], data_0054a888[], please_enter_format[];
extern int ToolHelpers_cc_GetNumFiles(void);
extern void ToolHelpers_cc_SetFileOutputName(int a, short b, char *s);
extern SInt32 ToolHelpers_cc_AddProjectEntry(OSSpec *path, SInt16 mode, char *name, Boolean flag, SInt32 fileId);
extern UInt8 data_00587e26;
extern UInt8 data_00587e27;
extern UInt8 data_00587e28;
#if VERSION >= VERSION_GC_2_7
extern int ToolHelpers_cc_AddAccessPath(char *spec, SInt16 use_first, int value, unsigned char option);
#else
extern int ToolHelpers_cc_AddAccessPath(char *spec, char use_first, int value, unsigned char option);
#endif
extern void ToolHelpers_cc_PassVirtualFileValuePair(char *fileData, struct StorageHandle **virtualFile);
extern void ToolHelpers_cc_CallFileInfoForDirectory(OSSpec *input);
extern int data_00587e04;
extern int data_00587e08;
extern int data_00587e0c;
extern const char *DAT_00543380;
extern char data_00540ad7;
extern char data_00540b16;
extern char data_00540b17;
extern char data_00540b18;
extern char data_00540b19;
extern char data_00540b1a;
extern char data_00540b1b;
extern char data_00540b1c;
extern char data_00540b1d;
extern char data_00540b1e;
extern char data_00540b1f;
extern char data_00540b20;
extern char data_00540b21;
extern char data_00588528;

#ifdef __cplusplus
}
#endif

#endif
