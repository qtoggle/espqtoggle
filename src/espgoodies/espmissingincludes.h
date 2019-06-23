
#ifndef _ESPGOODIES_ESPMISSINGINCLUDES_H
#define _ESPGOODIES_ESPMISSINGINCLUDES_H

#include <stdarg.h>
#include <ets_sys.h>

void ets_isr_mask(unsigned intr);
void ets_isr_unmask(unsigned intr);
int ets_sprintf(char *str, const char *format, ...)  __attribute__ ((format (printf, 2, 3)));
int ets_snprintf(char *str, size_t size, const char *format, ...) __attribute__ ((format (printf, 3, 4)));
int ets_vsnprintf(char *str, size_t size, const char *format, va_list args) __attribute__ ((format (printf, 3, 0)));

#define snprintf    ets_snprintf
#define vsnprintf   ets_vsnprintf


typedef signed short        int16;
typedef signed long long    int64;

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#endif /* _ESPGOODIES_ESPMISSINGINCLUDES_H */
