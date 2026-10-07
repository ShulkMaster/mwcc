#ifndef DRIVER_TARGETS_H
#define DRIVER_TARGETS_H

#include "compiler/common.h"
#include "version.h"
#include "compiler/win32.h"

#ifdef __cplusplus
extern "C" {
#endif

struct DispatchObject {
    unsigned char opaque_state[614];
    struct DispatchTable *dispatch;
};
struct DispatchTable {
    unsigned char reserved[8];
    int(__stdcall *invoke)(struct DispatchObject *, unsigned int, long *);
};
struct IndexedValueTable {
    unsigned char opaquePrefix[32];
    int count;
    unsigned int *values;
};
struct PtrList {
    int count;
    int size;
    char **items;
};
extern void Targets_FormatAndDispatchMessage(char *message, char *arguments);
extern void Targets_ReportFormattedMessage(char *a0, char *a1);
extern void format_and_dispatch_message(char *first, char *second);
extern void format_and_forward_message(char *message, unsigned int *arguments);
extern void format_and_report_message(const char *text, va_list position);
extern void report_operating_system_error(char *name, DWORD value, unsigned int *result);
extern char *Targets_GetResourceCString(SInt32 argument, char *buffer);
extern void Targets_DispatchVariadicMessage(int argument, ...);
extern unsigned char Targets_ReportOperatingSystemError(int id, short code, ...);
extern void Targets_FormatAndForwardMessage(int s, ...);
extern void Targets_ForwardVarArgsAndLongjmp(const char *fmt, ...);
extern void append_coalesced_argument(short kind, char *text);
extern void initialize_arguments(unsigned int value, char **otherValue);
extern void skip_whitespace_and_comments(void);
extern Boolean initialize_file_token_cursor(char *name);
extern unsigned int fn_0040f1df(void);
extern char *get_next_token(void);
extern unsigned char has_more_tokens_or_args(void);
extern void fn_0040f960(void);
extern unsigned int fn_0040f969(void);
extern TokenText *Targets_AdvanceArgument(void);
extern int Targets_IsValueNullOrZero(void);
extern int Targets_IncrementGlobalOnNonzeroResult(void);
extern TokenText *Targets_DecrementCountAndGetTokenText(void);
extern char *Targets_GetTokenTextDescription(TokenText *token);
extern void grow_ptr_list(PtrList *list);
extern void fn_0040fbe1(PtrList *list, short kind, char *text);
extern void terminate_ptr_list(PtrList *list);
extern void Targets_InitIntegerSequenceResult(PtrList *source, IntegerSequenceResult *result);
extern int Targets_SetTool(int *tool);
extern Boolean Targets_MatchTool(int cpu, int os, int lang, int type);
extern Boolean Targets_MatchCommandLineOptions(int argc, char **argv);
#if VERSION >= VERSION_GC_2_7
extern Boolean Targets_RegisterOptionLists(void);
#else
extern int Targets_RegisterOptionLists(void);
#endif
extern void Targets_InitPtrList(PtrList *state);
extern void fn_0040ecb1(SInt32 a, ...);
extern void Targets_ReportMessage(int a, ...);
extern char data_0054aa78;
extern char empty_string[];
extern char formatted_message[];
extern struct TokenText *token_texts;
extern int coalesced_argument_count;
extern int coalesced_argument_capacity;
extern unsigned char data_0057e06c;
extern int data_0057e070;
extern int arg_index;
extern char **data_0057e078;
extern unsigned char operation_record[];
extern unsigned char data_0057e1c0[];
extern char *token_cursor;
extern char *data_0057e1d0;
extern int data_0057e1d4;
extern int data_0057e1d8;
extern int data_005876ac;
extern char *data_00587eb0;
extern char *data_00587eec;
extern char argument_space;
extern char argument_space_char;
extern char data_00588519;
extern char data_0058852c;
extern char *get_next_arg(Boolean expand);
extern void tokenize_arguments(void);
extern void Targets_ParseArguments(int argc, char **argv);
extern void Targets_FreeTokenText(void);
extern char *Targets_CopyTokenText(TokenText *arg, char *buffer, int maxlen, Boolean warn);
extern void append_text_to_ptrlist_item(struct PtrList *list, char *text);
extern char data_00588505;
struct DispatchTable;
struct DispatchObject;

#ifdef __cplusplus
}
#endif

#endif
