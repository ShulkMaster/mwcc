#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/MemUtils.h"
#include "driver/CLBrowser.h"
#include "driver/CLErrors.h"
#include "driver/CLFiles.h"
#include "driver/CLPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/Memory.h"
#include <string.h>

/* Source and destination handles for a preference data copy. */
#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#if VERSION >= VERSION_GC_2_7
void *__stdcall xmalloc(const char *what, int size)
{
    void *memory;

    memory = malloc(size ? size : 1);
    if (!memory) {
        fprintf(stderr, "*** Out of memory when allocating %d bytes%s%s", size, what ? " for " : "", what ? what : "");
        exit(-23);
        return NULL;
    }
    return memory;
}

void *__stdcall xcalloc(const char *what, int size)
{
    void *memory;

    memory = xmalloc(what, size);
    memset(memory, 0, size);
    return memory;
}

void *__stdcall xrealloc(const char *what, void *old, int size)
{
    void *memory;

    memory = realloc(old, size ? size : 1);
    if (!memory) {
        fprintf(stderr, "*** Out of memory when resizing buffer to %d bytes%s%s", size, what ? " for " : "",
                what ? what : "");
        exit(-23);
        return NULL;
    }
    return memory;
}

char *__stdcall xstrdup(const char *s)
{
    return strcpy(xmalloc(NULL, strlen(s) + 1), s);
}
#else
void *xmalloc(const char *text, unsigned int size)
{
    char message[80];
    char *memory;

    memory = malloc(size ? size : 1);
    if (memory == NULL) {
        sprintf(message, "Out of memory when allocating %d bytes%s%s", size, text ? " for " : "", text ? text : "");
        CLErrors_FatalError(message);
        longjmp(driver_jmp_buf, 1);
        return NULL;
    }
    return memory;
}

void *xcalloc(const char *pool, unsigned int size)
{
    unsigned char *memory;
    memory = xmalloc(pool, size);
    memset(memory, 0, size);
    return memory;
}

void *xrealloc(const char *detail, void *block, unsigned int size)
{
    char message[80];
    char *resizedBlock;

    resizedBlock = (char *)realloc(block, size ? size : 1U);
    if (!resizedBlock) {
        sprintf(message, "Out of memory when resizing buffer to %d bytes%s%s", size, detail ? " for " : "",
                detail ? detail : "");
        CLErrors_FatalError(message);
        longjmp(driver_jmp_buf, 1U);
        return NULL;
    }
    return resizedBlock;
}

char *xstrdup(const char *s)
{
    return strcpy(xmalloc(NULL, strlen(s) + 1), s);
}

#endif

int __stdcall MemUtils_CallPluginEntry(Plugin *entry)
{
    short result;
    result = CLPlugins_CallEntry(entry, entry->object);
    return result;
}

#pragma auto_inline reset
