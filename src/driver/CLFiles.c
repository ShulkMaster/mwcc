#include "compiler/common.h"
#include "driver/CLFiles.h"
#include "compiler/InlineAsmPPC.h"
#include "driver/CLFileOps.h"
#include "driver/CLIO.h"
#include "driver/CLOverlays.h"
#include "driver/CLPlugins.h"
#include "driver/CLPrefs.h"
#include "driver/MemUtils.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
/* Link used by the file cleanup list. */
#include <string.h>

#include <stdlib.h>
#include <stddef.h>

struct DropinFileRecord *CLFiles_AllocDropinFileRecord(void)
{
    const unsigned int bufferSize = sizeof(struct DropinFileRecord);
    struct DropinFileRecord *buffer;

    buffer = xmalloc(NULL, bufferSize);
    if (buffer == NULL) {
        return NULL;
    }
    memset(buffer, 0, bufferSize);
    return buffer;
}

void free_allocation_record(void *ptr)
{
    DropinFileRecord *allocation = ptr;

    if (allocation != NULL) {
#if VERSION >= VERSION_GC_2_7
        if (allocation->outputStorage != NULL) {
            Memory_FreeHandle(allocation->outputStorage);
            allocation->outputStorage = NULL;
        }
#endif
        if (allocation->objectData != NULL) {
            Memory_FreeHandle(allocation->objectData);
            allocation->objectData = NULL;
        }
        if (allocation->secondaryReferenceHandle != NULL) {
            Memory_FreeHandle(allocation->secondaryReferenceHandle);
            allocation->secondaryReferenceHandle = NULL;
        }
#if VERSION >= VERSION_GC_2_7
        xfree(allocation);
#else
        free(ptr);
#endif
    }
}

unsigned char CLFiles_FreeAllocationRecords(struct IndexedListLink *list)
{
    struct IndexedListLink *next;
    struct IndexedListLink *entry;

    if (list == NULL) {
#if VERSION >= VERSION_GC_2_7
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 56);
#else
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 0x32);
#endif
    }
    entry = list->next;
    while (entry != NULL) {
        next = entry->next;
        free_allocation_record(entry);
        entry = next;
    }
    return 1;
}

/* Word-based argument record passed to the file helper. */

unsigned char CLFiles_InsertIndexedListLinkAtFirstIndex(IndexedListLink *first, IndexedListLink *second)
{
    return CLFiles_InsertIndexedListLink(first, second, first->index);
}

SInt32 CLFiles_GetIndex(IndexedListLink *entry)
{
    if (!entry)
#if VERSION >= VERSION_GC_2_7
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 127U);
#else
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 121U);
#endif
    return entry->index;
}

Boolean CLFiles_InitChain(struct ChainRecord **chain)
{
    *chain = NULL;
    return 1;
}

void CLFiles_FreeChainNodes(struct ChainRecord **nodes)
{
    struct ChainRecord *next;
    StorageHandle *record;

    while (*nodes != NULL) {
        next = (*nodes)->next;
        record = (*nodes)->object;
        Memory_FreeHandle(record);
#if VERSION >= VERSION_GC_2_7
        xfree(*nodes);
#else
        free(*nodes);
#endif
        nodes = &next;
    }
    *nodes = NULL;
}

struct ChainRecord *CLFiles_CreateChainRecord(const char *source, StorageHandle *argument)
{
    struct ChainRecord *record;
    StorageHandle *buffer;
    record = xmalloc(NULL, sizeof(*record));
    if (record == NULL)
        return NULL;
    strncpy(record->name, source, sizeof(record->name));
    record->name[sizeof(record->name) - 1] = 0;
    record->object = (StorageHandle *)Memory_NewHandle(0U);
    if (record->object == NULL || fn_00413b50(argument, record->object) != 0) {
#if VERSION >= VERSION_GC_2_7
        xfree(record);
#else
        free(record);
#endif
        return NULL;
    }
    buffer = record->object;
    CLPrefs_ConvertLoneLFToCR(buffer);
    record->next = NULL;
    return record;
}

Boolean CLFiles_AppendChainRecord(struct ChainRecord **link, struct ChainRecord *record)
{
    for (; *link != NULL; link = &(*link)->next) {
    }
    *link = record;
    return 1;
}

ChainRecord *CLFiles_FindChainRecord(ChainRecord *first, const char *value)
{
    ChainRecord *input;
    const char *argument;
    ChainRecord *record;
    int empty;
    input = first;
    empty = (input == NULL);
    argument = value;
    record = input;
    if (!empty) {
        do {
            if (!strcmp(record->name, argument))
                break;
            record = record->next;
        } while (record);
    }
    return record;
}

/* Linked entries identified by a nonnegative index. */

DropinFileRecord *CLFiles_FindFileByIndex(IndexedListLink *head, int index)
{
    IndexedListLink *entry;
    if (!head)
#if VERSION >= VERSION_GC_2_7
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 104U);
#else
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 98U);
#endif
    if (index < 0)
#if VERSION >= VERSION_GC_2_7
        CLIO_ReportAssertionFailure("filenum >= 0", "CLFiles.c", 105U);
#else
        CLIO_ReportAssertionFailure("filenum >= 0", "CLFiles.c", 99U);
#endif
    entry = head->next;
    while (entry && entry->index != index)
        entry = entry->next;
    return (DropinFileRecord *)((char *)entry - offsetof(DropinFileRecord, listEntry));
}

unsigned char CLFiles_AssertNonNullIndexedListLink(struct IndexedListLink *value)
{
    if (value == NULL)
#if VERSION >= VERSION_GC_2_7
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 47U);
#else
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 41U);
#endif
    return 1;
}

struct DropinFileRecord *CLFiles_FindDropinFileRecord(struct IndexedListLink *files, const struct OSSpec *fileSpec)
{
    struct DropinFileRecord *record =
        (struct DropinFileRecord *)((char *)files->next - offsetof(struct DropinFileRecord, listEntry));
    while (record != NULL && OS_EqualSpec(&record->inputPath, fileSpec) == 0)
        record =
            (struct DropinFileRecord *)((char *)record->listEntry.next - offsetof(struct DropinFileRecord, listEntry));
    return record;
}

char CLFiles_InsertIndexedListLink(IndexedListLink *list, IndexedListLink *node, int pos)
{
    IndexedListLink *p;
    if (list == NULL) {
#if VERSION >= VERSION_GC_2_7
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 80);
#else
        CLIO_ReportAssertionFailure("this != NULL", "CLFiles.c", 74);
#endif
    }
    if (node == NULL) {
#if VERSION >= VERSION_GC_2_7
        CLIO_ReportAssertionFailure("file != NULL", "CLFiles.c", 81);
#else
        CLIO_ReportAssertionFailure("file != NULL", "CLFiles.c", 75);
#endif
    }
    if (pos < 0) {
        pos = 0;
    } else if (pos > list->index) {
        pos = list->index;
    }
    p = list;
    while (pos-- > 0) {
        p = p->next;
    }
    node->index = list->index++;
    node->next = p->next;
    p->next = node;
    return 1;
}
