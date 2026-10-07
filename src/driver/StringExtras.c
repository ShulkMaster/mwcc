#include "driver/StringExtras.h"
#include <ctype.h>

#if VERSION >= VERSION_GC_2_7
int __stdcall ustrcmp(const char *left, const char *right)
{
    int difference;

    do {
        difference = tolower(*left) - tolower(*right++);
        if (difference)
            return difference;
    } while (*left++);

    return 0;
}
#endif
