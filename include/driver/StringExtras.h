#ifndef DRIVER_STRINGEXTRAS_H
#define DRIVER_STRINGEXTRAS_H

#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VERSION >= VERSION_GC_2_7
extern int __stdcall ustrcmp(const char *left, const char *right);
#endif

#ifdef __cplusplus
}
#endif

#endif
