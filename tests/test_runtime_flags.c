/* Compile with the NDK and execute on the target Android device. */
#include "recomp_types.h"
#include <assert.h>

static unsigned even_parity(unsigned value)
{
    unsigned bits = 0;
    for (unsigned bit = 0; bit < 8; ++bit) bits += (value >> bit) & 1u;
    return !(bits & 1u);
}

int main(void)
{
    const unsigned shifts[] = {0, 16, 24};
    for (unsigned a = 0; a < 256; ++a) for (unsigned b = 0; b < 256; ++b) {
        unsigned sub = even_parity((a + 256 - b) % 256);
        unsigned add = even_parity((a + b) % 256);
        unsigned test = even_parity(a & b);
        assert((unsigned)CMP_P(a, b) == sub);
        assert((unsigned)CMP_NP(a, b) == !sub);
        for (unsigned i = 0; i < sizeof shifts / sizeof *shifts; ++i) {
            unsigned shift = shifts[i], left = a << shift, right = b << shift;
            assert((unsigned)recomp_cond(FK_NARROW(FK_CMP, shift), left, right, CC_P) == sub);
            assert((unsigned)recomp_cond(FK_NARROW(FK_CMP, shift), left, right, CC_NP) == !sub);
            assert((unsigned)recomp_cond(FK_NARROW(FK_ADD, shift), left, right, CC_P) == add);
            assert((unsigned)recomp_cond(FK_NARROW(FK_TEST, shift), left, right, CC_P) == test);
            assert((unsigned)RECOMP_PF(left & right, shift) == test);
        }
    }
    assert(recomp_cond(FK_FCOM, 2, 0, CC_P));
    assert(!recomp_cond(FK_FCOM, 2, 0, CC_NP));
    assert(!recomp_cond(FK_FCOM, (uint32_t)-1, 0, CC_P));
    assert(recomp_cond(FK_FCOM, 0, 0, CC_NP));
    puts("runtime parity PASS: 65,536 pairs, widths 8/16/32, CMP/ADD/TEST and unordered x87");
    return 0;
}
