#ifndef VERSION_H
#define VERSION_H

#define VERSION_GC_1_2_5 0
#define VERSION_GC_1_2_5N 1
#define VERSION_GC_1_3 2
#define VERSION_GC_2_7 3

/* configure.py passes -DVERSION=<index> */
#ifndef VERSION
#define VERSION VERSION_GC_1_2_5
#endif

#endif
