#ifndef DRIVER_MEMUTILS_H
#define DRIVER_MEMUTILS_H

#include <setjmp.h>
#include "version.h"
#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VERSION >= VERSION_GC_2_7
extern void *__stdcall xmalloc(const char *what, int size);
extern void *__stdcall xcalloc(const char *what, int size);
extern void *__stdcall xrealloc(const char *what, void *old, int size);
extern char *__stdcall xstrdup(const char *s);
extern void __stdcall xfree(void *ptr);
#else
extern void *xmalloc(const char *text, unsigned int size);
extern void *xcalloc(const char *pool, unsigned int size);
extern void *xrealloc(const char *detail, void *a1, unsigned int size);
extern char *xstrdup(const char *s);
#endif
extern int __stdcall MemUtils_CallPluginEntry(Plugin *entry);

#ifdef __cplusplus
}
#endif

#endif
