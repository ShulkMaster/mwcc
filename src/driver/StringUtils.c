#include "compiler/common.h"
#include "driver/StringUtils.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/win32.h"
#include "driver/CLIO.h"
#include "driver/Memory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "version.h"

UInt8 *_pstrcpy(UInt8 *destination, UInt8 *string)
{
    memcpy(destination, string, *string + 1);
    return destination;
}

unsigned int pstrcmp(UInt8 *left, char *right)
{
    int length;
    unsigned int difference;

    if ((difference = (length = *left++) - ((UInt8)*right++)) > 0) {
        return difference;
    }
    while (length-- > 0) {
        if ((difference = (*left++ != (UInt8)*right++))) {
            return difference;
        }
    }
    return 0;
}

int pstrchr(UInt8 *characters, char character)
{
    int index;

    index = 0;
    while (index++ < *characters) {
        if (character == characters[index]) {
            return index;
        }
    }
    return 0;
}

void __stdcall c2pstrcpy(unsigned char *dst, const char *src)
{
    int length;

    length = strlen(src);
    if (0xff < length) {
        length = 0xff;
    }
    memmove(1 + dst, src, length);
    *dst = (char)length;
}

void __stdcall p2cstrcpy(char *destination, UInt8 *source)
{
    memcpy(destination, (source + 1), *source);
    destination[*source] = 0;
}

char *mvprintf(char *buffer, unsigned int size, const char *format, va_list args)
{
    int capacity;
    char *result;
    int length;

    if (buffer == NULL)
#if VERSION >= VERSION_GC_2_7
        CLIO_ReportAssertionFailure("mybuf != NULL", "StringUtils.c", 134U);
#else
        CLIO_ReportAssertionFailure("mybuf != NULL", "StringUtils.c", 135U);
#endif

    capacity = size - 1;
    result = buffer;
    length = vsnprintf(buffer, capacity, format, args);
    if (length < 0) {
        do {
            if (result != buffer)
                free(result);
            capacity <<= 1;
            result = malloc(capacity);
            if (result == NULL)
                return strncpy(buffer, "<out of memory>", size);
            length = vsnprintf(result, capacity, format, args);
        } while (length < 0);
    } else if (length > capacity) {
        capacity = length + 1;
        result = malloc(capacity);
        if (result == NULL)
            return strncpy(buffer, "<out of memory>", size);
        vsnprintf(result, capacity, format, args);
    }
    return result;
}

char *mprintf(char *buffer, int size, char *format, ...)
{
    va_list args;
    char *result;

    va_start(args, format);
    result = mvprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

int HPrintF(struct StorageHandle *output, char *format, ...)
{
    char *text;
    int length;
    va_list arguments;

    va_start(arguments, format);
    text = mvprintf(hprintf_buffer, 0x100, format, arguments);
    length = strlen(text);
    if (Memory_AppendStorageHandle(text, output, strlen(text)) != 0) {
        return 0;
    }
    if (text != hprintf_buffer) {
        free(text);
    }
    return length;
}

#pragma auto_inline reset
