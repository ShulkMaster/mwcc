#ifndef DRIVER_CC_EABI_PPC_H
#define DRIVER_CC_EABI_PPC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct LanguageSettings {
    unsigned char options[14];
};
struct SymbolSettings {
    unsigned char preferenceData[2]; /* 0x00: initialize_copts copies the opaque prefix from settingsHandle.symbols. */
    unsigned char deleteDeadInstructions; /* 0x02: initialize_copts copies this to copts.deleteDeadInstructions. */
    unsigned char
        spillBlockWeightMode; /* 0x03: initialize_copts enables copts.uniformSpillBlockWeight when this is 1. */
    unsigned char
        trailingPreferenceData[8]; /* 0x04: initialize_copts copies the opaque tail from settingsHandle.symbols. */
};
extern void cc_eabi_ppc_ReportCompilingFunction(char *name);
extern int __stdcall dispatch_compiler_plugin_request(CWPluginPrivateContext *input);
extern void initialize_copts(CPrepCU *source);
extern signed char initialize_compiler_plugin_cu(CWPluginPrivateContext *context);
extern unsigned char data_0054c408[];
extern struct DriverSettings {
    unsigned char b[9];
    char name[32];
    unsigned char options[15];
    SInt16 inlineLimit; /* 0x38: cc-eabi-ppc.c copies this into copts.inlineLimit */
    unsigned char tail[4];
} data_0057f448;
extern char data_0057f486;
extern int data_00588258;
extern unsigned char data_005ef387;
extern struct CPrepCU compiler_plugin_cu;
extern char compiler_plugin_target_name[128];
extern char mm_extension[], c_extension[], uppercase_m_extension[];
extern char data_0054c464[];
extern char data_0054c474[], data_0054c464[];

#ifdef __cplusplus
}
#endif

#endif
