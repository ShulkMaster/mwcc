#include "compiler/common.h"
#include "version.h"
#include "driver/CLAccessPaths.h"
#include "compiler/win32.h"
#include "driver/CLDependencies.h"
#include "driver/CLIO.h"
#include "driver/CLPluginRequests.h"
#include "driver/CLProj.h"
#include "driver/CLTarg.h"
#include "driver/CWParserPluginsPrivate.h"
#include "driver/Files.h"
#include "driver/MacSpecs.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include <string.h>
#include <stdlib.h>

#if VERSION >= VERSION_GC_2_7
#define FREE_MEMORY(ptr) xfree(ptr)
#define ASSERT_LINE(old, current) (current)
#else
#define FREE_MEMORY(ptr) free(ptr)
#define ASSERT_LINE(old, current) (old)
#endif

AccessPathEntry *init_access_path_entry(OSPathBuffer *src, AccessPathEntry *result)
{
    result->path = xmalloc(NULL, sizeof(*src));
    *(OSPathBuffer *)result->path = *src;
    result->children = NULL;
    result->auxiliary2 = 0;
#if VERSION >= VERSION_GC_2_7
    result->flags = 0;
#endif
    return result;
}

AccessPathEntry *CLAccessPaths_CreateAccessPathEntry(char *source)
{
    AccessPathEntry *result;
    OSPathBuffer *path = (OSPathBuffer *)source;

    result = xmalloc(NULL, 0x10);
    return init_access_path_entry(path, result);
}

void free_access_path_entry(AccessPathEntry *blocks)
{
    if (blocks != NULL) {
        if (blocks->path != NULL) {
            FREE_MEMORY(blocks->path);
        }
        if (blocks->children != NULL) {
            CLAccessPaths_FreeItems(blocks->children);
            FREE_MEMORY(blocks->children);
        }
        FREE_MEMORY(blocks);
    }
}

void copy_access_paths_to_file_specs_checked(CWFileSpec *path, AccessPaths *value, short result)
{
    copy_access_paths_to_file_specs(&path, value, &result);
    if ((unsigned short)result != 0)
        CLIO_ReportAssertionFailure("count == 0", "CLAccessPaths.c", ASSERT_LINE(357, 367));
}

/* Array of access-path entries and its 16-bit bookkeeping. */

unsigned char CLAccessPaths_FreeItems(AccessPaths *paths)
{
    unsigned int index;
    if (!paths)
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", 63U);
    if (paths->items) {
        for (index = 0; (unsigned short)index < paths->count; ++index)
            free_access_path_entry(paths->items[(unsigned short)index]);
        FREE_MEMORY(paths->items);
    }
    paths->items = NULL;
    return 1;
}

/* Indexed values and their 16-bit count. */

unsigned char CLAccessPaths_Init(AccessPaths *paths)
{
    if (paths == NULL) {
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", 52U);
    }
    memset(paths, 0, sizeof(*paths));
    paths->items = NULL;
    return 1;
}
/* Indexed entries with a 16-bit count. */

/* Access-path entry holding separately allocated data. */

unsigned char CLAccessPaths_InitializeChildren(AccessPathEntry *path)
{
    path->children = xcalloc(0U, 8U);
    add_subdirectory_access_paths(path->children, path);
    return (unsigned char)(CLAccessPaths_GetCount(path->children) != 0);
}

int count_access_paths_recursive(AccessPaths *accessPaths)
{
    unsigned short index;
    int count;
    AccessPathEntry *entry;

    count = CLAccessPaths_GetCount(accessPaths);
    for (index = 0; index < count; index++) {
        entry = CLAccessPaths_GetEntry(accessPaths, index);
        if (entry == NULL)
            CLIO_ReportAssertionFailure("path", "CLAccessPaths.c", ASSERT_LINE(326, 336));
        if (entry->children)
            count += count_access_paths_recursive(entry->children);
    }
    return count;
}

/* An access-path entry and its nested list. */
void copy_access_paths_to_file_specs(CWFileSpec **destination, AccessPaths *paths, short *remaining)
{
    int validEntry;
    AccessPathEntry *entry;
    UInt16 index;
    OSSpec spec;

    for (index = 0; index < CLAccessPaths_GetCount(paths); index = index + 1) {
        entry = CLAccessPaths_GetEntry(paths, index);
        validEntry = 0;
        if ((entry != NULL) && (*remaining != 0)) {
            validEntry = 1;
        }
        if (!validEntry) {
            CLIO_ReportAssertionFailure("path && *count > 0", "CLAccessPaths.c", ASSERT_LINE(343, 353));
        }
        CLProj_MakeOSSpecFromPath(entry->path, NULL, '\0', &spec);
        if (MacSpecs_MakeCWFileSpecFromString(spec.directory.path, *destination) == 0) {
            CLIO_ReportAssertionFailure("OS_OSSpec_To_FSSpec(&spec, *list)", "CLAccessPaths.c", ASSERT_LINE(345, 355));
        }
        *destination += 1;
        *remaining -= 1;
        if (entry->children != NULL) {
            copy_access_paths_to_file_specs(destination, entry->children, remaining);
        }
    }
    return;
}

/* Records and indexed table used by access-path lookup. */

AccessPathEntry *CLAccessPaths_FindPath(AccessPaths *table, void *value)
{
    unsigned int index;
    AccessPathEntry *entry;

    if (!table)
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", ASSERT_LINE(173, 182));
    for (index = 0; (unsigned short)index < table->count; ++index) {
        entry = table->items[(unsigned short)index];
        if (OS_EqualPathSpec(entry->path, value))
            return entry;
    }
    return NULL;
}

Boolean CLAccessPaths_InsertItem(AccessPaths *paths, UInt16 index, void *value)
{
    UInt16 n;
    if (allocate_slot(paths, &n)) {
        if (index > n)
#if VERSION >= VERSION_GC_2_7
            index = n != 0 ? n - 1 : 0;
#else
            index = n - 1;
#endif
        memmove(&paths->items[index + 1], &paths->items[index], (UInt32)(paths->count - index) * 4);
        paths->items[index] = value;
        return 1;
    }
    return 0;
}

Boolean add_subdirectory_access_paths(AccessPaths *ctx, AccessPathEntry *param2)
{
    DirectorySearch state;
    OSSpec bufB;
#if VERSION >= VERSION_GC_2_7
    char bufC[256];
#else
    char bufC[67];
#endif
    Boolean flag;
    DWORD status;
    int count = 0;
    char *name;
    char *end;
    unsigned short s;
    AccessPathEntry *ptr;

    if (fn_004151f0())
        return 1;
    status = OS_OpenDir(param2->path, &state);
    while (status == 0) {
        status = OS_ReadDir(&state, &bufB, bufC, &flag);
        if (status == 0) {
            if (flag == 0) {
                ptr = CLAccessPaths_CreateAccessPathEntry(bufB.directory.path);
                name = CLProj_GetFileName(bufC);
#if VERSION >= VERSION_GC_2_7
                end = name + strlen(name) - 1;
                if (*end == '\\')
                    end--;
                if (end > name && *name == '(' && *end == ')')
                    continue;
#else
                if (*name == '(' || name[strlen(name) - 2] == ')')
                    continue;
#endif
                if (!CLAccessPaths_StoreItem(ctx, ptr))
                    break;
                s = CLAccessPaths_GetCount(ctx) - 1;
                if (!add_subdirectory_access_paths(ctx, ptr))
#if VERSION >= VERSION_GC_2_7
                    delete_access_path(ctx, s);
#else
                    remove_access_path(ctx, s);
#endif
            } else {
                count++;
                if (count % 50 == 0 && fn_004151f0())
                    return 0;
            }
        }
    }
    OS_CloseDir(&state);
    return count > 0;
}

unsigned short CLAccessPaths_GetCount(AccessPaths *table)
{
    if (!table) {
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", ASSERT_LINE(152, 161));
    }
    return table->count;
}

AccessPathEntry *CLAccessPaths_GetEntry(AccessPaths *table, unsigned short index)
{
    if (table == NULL)
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", ASSERT_LINE(142, 151));
    if ((unsigned short)index < table->count)
        return table->items[(unsigned short)index];
    return 0U;
}

#if VERSION >= VERSION_GC_2_7
unsigned char remove_access_path(AccessPaths *list, UInt16 index)
{
    if (index >= list->count)
        return 0;
    memmove(&list->items[index], &list->items[index + 1], (list->count - index - 1) * sizeof(*list->items));
    list->count--;
    return 1;
}

unsigned char delete_access_path(AccessPaths *list, UInt16 index)
{
    if (index >= list->count)
        return 0;
    free_access_path_entry(list->items[index]);
    return remove_access_path(list, index);
}
#else
unsigned char remove_access_path(AccessPaths *list, UInt16 index)
{
    if (index >= list->count)
        return 0;
    free_access_path_entry(list->items[index]);
    memmove(&list->items[index], &list->items[index + 1], (list->count - index) * sizeof(*list->items));
    list->count--;
    return 1;
}
#endif

unsigned char CLAccessPaths_StoreItem(AccessPaths *ctx, void *value)
{
    UInt16 idx;
    if (allocate_slot(ctx, &idx)) {
        ctx->items[idx] = value;
        return 1;
    }
    return 0;
}

Boolean allocate_slot(AccessPaths *table, UInt16 *slotIndex)
{
    if (table == NULL) {
        CLIO_ReportAssertionFailure("paths != NULL", "CLAccessPaths.c", 84U);
    }
    if (table->count >= table->capacity) {
        table->capacity += 20U;
        table->items = xrealloc("access paths", table->items, table->capacity << 2);
    }
    *slotIndex = table->count++;
    return 1;
}
