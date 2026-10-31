/* The reserved guest window cannot contain native GPR/MMX storage. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#if defined(RECOMP_LOCAL_REGS) || defined(RECOMP_PLAIN_MEMORY) || defined(RECOMP_GUEST_PROVENANCE)
#error "This compiler profile requires global registers and volatile guest memory"
#endif
#undef RECOMP_MEM_ADDR
#define RECOMP_MEM_ADDR(va) RECOMP_SEPARATE_MEM_ADDR(va)
