#ifndef UT3_NATIVE_MODE_INC
#define UT3_NATIVE_MODE_INC

#include <stdlib.h>
#include <string.h>

/* Selected before guest startup; exact takes precedence over diagnostic files. */
static inline int void_native_mode(int module_mode)
{
    const char *mode = getenv("UT3_EXECUTION_MODE");
    return mode && !strcmp(mode, "exact") ? 0 : module_mode;
}

#endif
