#define CERROR_FILE "unknown.c"
#include "version.h"
#include "compiler/common.h"
#include "driver/CLStaticPlugins.h"
#include "driver/cc-eabi-ppc-mw.h"
#if VERSION >= VERSION_GC_2_7
#include "driver/libimp-eabi-ppc.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <msl_internal.h>

#if VERSION >= VERSION_GC_2_7
void CLStaticPlugins_SetIdentifiers(SInt32 *architectureIdentifier, SInt32 *abiIdentifier)
{
    *architectureIdentifier = 0x65505043;
    *abiIdentifier = 0x45414249;
}

void fn_004053a0(SInt32 *pluginType, SInt32 *pluginSubtype)
{
    *pluginType = '****';
    *pluginSubtype = 'Comp';
}
#else
unsigned int CLStaticPlugins_SetIdentifiers(SInt32 *architectureIdentifier, SInt32 *abiIdentifier)
{
    *(unsigned int *)architectureIdentifier = 0x65505043U;
    *abiIdentifier = 0x45414249U;
    return (unsigned int)abiIdentifier;
}

unsigned int fn_004053a0(SInt32 *pluginType, SInt32 *pluginSubtype)
{
    *(unsigned int *)pluginType = '****';
    *pluginSubtype = 'Comp';
    return (unsigned int)pluginSubtype;
}

#endif

void fn_004053c0(SInt32 *pluginID)
{
    *pluginID = 'Seep';
}

unsigned int fn_004053d0(void)
{
    int success;

    success = 0;
    if (fn_0040bed0() != 0 && fn_0040c060() != 0) {
        success = 1;
    }
    return success;
}

int fn_004053f0(void)
{
#if VERSION >= VERSION_GC_2_7
    return fn_0040bf10() && RegisterLibImporterResources();
#else
    return fn_0040bf10() && fn_0040c070();
#endif
}
