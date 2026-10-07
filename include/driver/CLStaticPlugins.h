#ifndef DRIVER_CLSTATICPLUGINS_H
#define DRIVER_CLSTATICPLUGINS_H

#include "version.h"
#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int fn_004053d0(void);
extern int fn_004053f0(void);
#if VERSION >= VERSION_GC_2_7
extern void CLStaticPlugins_SetIdentifiers(SInt32 *architectureIdentifier, SInt32 *abiIdentifier);
extern void fn_004053a0(SInt32 *pluginType, SInt32 *pluginSubtype);
#else
extern unsigned int CLStaticPlugins_SetIdentifiers(SInt32 *a0, SInt32 *a1);
extern unsigned int fn_004053a0(SInt32 *a0, SInt32 *a1);
#endif
extern void fn_004053c0(SInt32 *a0);

#ifdef __cplusplus
}
#endif

#endif
