#ifndef DRIVER_CWPARSERPLUGINSPRIVATE_H
#define DRIVER_CWPARSERPLUGINSPRIVATE_H

#include "compiler/common.h"
#include "driver/Files.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CommandLineArguments {
    int argc;
    char **argv;
};
#pragma pack(push, 1)
struct FileOperationInfo {
    CWFileSpec pathSpec;
    SInt32 position;
    Boolean recursive;
    UInt8 type;
};
#pragma pack(pop)
struct IntegerSequenceResult {
    unsigned int count;
    int *entries;
    unsigned int value;
};
#pragma options align = mac68k
struct PanelEntry {
    UInt32 type;            /* 0x00: ToolHelpers-cc.c selects compiler, parser and driver tool versions */
    UInt32 creator;         /* 0x04: ToolHelpers-cc.c matches driverTool creator */
    UInt32 flags;           /* 0x08: ToolHelpers.c selects linker flags */
    UInt32 version;         /* 0x0c: ToolHelpers-cc.c passes to format_version */
    UInt8 enabled;          /* 0x10: initialize_cmdline_environment tests panel availability */
    UInt8 alignmentPadding; /* 0x11: ToolHelpers.c panel array stride includes unused trailing alignment byte */
};
#pragma options align = reset
struct ValuePairState {
    char *firstValue;
    unsigned int secondValue;
    unsigned char flag;
};
extern CWPluginPrivateContext *validate_parser_context(CWPluginPrivateContext *header);
extern unsigned int __stdcall CWParserPluginsPrivate_GetParserValues(CWPluginPrivateContext *query,
                                                                     unsigned int *first_value,
                                                                     unsigned int *second_value);
extern int __stdcall CWParserPluginsPrivate_GetEnvironment(void *input, CommandLineArguments **value_out);
extern int __stdcall CWParserPluginsPrivate_GetOutputValues(struct CWPluginPrivateContext *context,
                                                            unsigned char *firstValue, unsigned char *secondValue);
extern int __stdcall CWParserPluginsPrivate_GetPanels(void *context, int *firstValue, struct PanelEntry **secondValue);
extern int __stdcall CWParserPluginsPrivate_GetLookupValues(void *context, int *firstValue, char ***secondValue);
extern int __stdcall CWParserPluginsPrivate_SetParserEntry(void *ctx, int index, IntegerSequenceResult *value);
extern int __stdcall fn_0041bfd0(void *context, char *name, void **value);
extern unsigned int __stdcall CWParserPluginsPrivate_CallFileOperationCallback(CWPluginPrivateContext *objectId,
                                                                               FileOperationInfo *argument);
extern int __stdcall CWParserPluginsPrivate_CallFileInfo(CWPluginPrivateContext *ctx, CWFileSpec *info);
extern int __stdcall CWParserPluginsPrivate_CallParserTextCallback(CWPluginPrivateContext *request, int argument2,
                                                                   int argument3, char *text);
extern int __stdcall CWParserPluginsPrivate_PassValuePair(CWPluginPrivateContext *target, char *firstValue,
                                                          unsigned int secondValue);
extern void __stdcall CWParserPluginsPrivate_CallValuePairCallback(CWPluginPrivateContext *target, char *firstValue,
                                                                   unsigned int secondValue);
struct PanelEntry;
struct IntegerSequenceResult;

#ifdef __cplusplus
}
#endif

#endif
