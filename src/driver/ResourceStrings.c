#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/ResourceStrings.h"
#include <stdlib.h>
#include <stdio.h>
#include "version.h"

#if VERSION >= VERSION_GC_2_7
#define FORMAT_RESOURCE_STRING(...) snprintf(resource_string_buffer, 256, __VA_ARGS__)
#else
#define FORMAT_RESOURCE_STRING(...) sprintf(resource_string_buffer, __VA_ARGS__)
#endif

int ResourceStrings_AddResource(char *resourceData, short resourceId, char **resourceValue)
{
    int i;

    for (i = 0; i < 16 && resourceRegistrations[i].resourceId != 0; ++i) {
        if (resourceId == resourceRegistrations[i].resourceId) {
            fprintf(stderr, "Resource %d is already added!\n", resourceId);
            return 0;
        }
    }
    if (i >= 16) {
        return 0;
    }
    resourceRegistrations[i].data = resourceData;
    resourceRegistrations[i].resourceId = resourceId;
    resourceRegistrations[i].value = resourceValue;
    return 1;
}

char *ResourceStrings_GetString(short id, short index)
{
    int i;
    int j;
    index--;
    for (i = 0; i < 16; i++) {
        if (id == resourceRegistrations[i].resourceId) {
            j = 0;
            if (index < 0) {
                FORMAT_RESOURCE_STRING("[Illegal string index #%d in list '%s' (%d)]", index,
                        resourceRegistrations[i].data, id);
                return resource_string_buffer;
            }
            while (j <= index) {
                if (resourceRegistrations[i].value[j] == NULL) {
                    FORMAT_RESOURCE_STRING("[String #%d not found in resource '%s' (%d)]", index + 1,
                            resourceRegistrations[i].data, id);
                    return resource_string_buffer;
                }
                if (j == index)
                    return resourceRegistrations[i].value[j];
                j++;
            }
        }
    }
    return NULL;
}
