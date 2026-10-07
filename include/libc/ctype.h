#ifndef MWCC_HOST_PROBE_CTYPE_H
#define MWCC_HOST_PROBE_CTYPE_H

#include "version.h"

/* The MSL ctype functions the compiler links (lib/msl/MSL_Common/Include/cctype). */
#if VERSION >= VERSION_GC_2_7
extern unsigned char __lower_map[];
inline int tolower(int c)
{
    return c == -1 ? -1 : (int)__lower_map[(unsigned char)c];
}
#else
int tolower(int c);
#endif

#endif
