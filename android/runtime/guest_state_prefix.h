/* The reserved guest window cannot contain native GPR/MMX storage. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
/* Local registers are allowed together with the masked call protocol (tools/protocol_transform.py),
 * which publishes and reloads exactly the registers the call-protocol summary proves live. */
#if defined(RECOMP_PLAIN_MEMORY) || defined(RECOMP_GUEST_PROVENANCE)
#error "This compiler profile requires volatile guest memory"
#endif
#undef RECOMP_MEM_ADDR
#define RECOMP_MEM_ADDR(va) RECOMP_SEPARATE_MEM_ADDR(va)
