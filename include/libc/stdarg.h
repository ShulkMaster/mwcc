#ifndef MWCC_HOST_PROBE_STDARG_H
#define MWCC_HOST_PROBE_STDARG_H

#ifndef __va_list__
#define __va_list__
typedef char *va_list;
#endif

/* MSL_Common/Include/cstdarg, Intel host ABI. */
#define va_start(ap, parm) (ap = (va_list)((long)(&(parm)) + ((((long)(&(parm) + 1) - (long)(&(parm))) + 3) / 4 * 4)))
#define va_end(ap)

#endif
