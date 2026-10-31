/* Additional VC8 imports over the host's existing drive and code-page model. */
#include "host.h"

CDECL_EXPORT("msvcrt.dll", _getdrives) { return 1u << 2; }

WINAPI_EXPORT("kernel32.dll", IsDBCSLeadByteEx, 2)
{
    /* The code pages supported by GetCPInfo contain no DBCS lead bytes. */
    switch (ARG(0)) {
    case 0: case 1: case 3: case 437: case 1251: case 1252:
    case 20127: case 28591: case 65001: return 0;
    default: void_set_last_error(ERROR_INVALID_PARAMETER_); return 0;
    }
}
