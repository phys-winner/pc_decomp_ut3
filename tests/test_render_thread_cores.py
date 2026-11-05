"""HOST-run the production render-thread core choice (render_core_mask of android/host/d3d9_thread.c).

The pure function is taken from the source as it is and run on big/LITTLE tables: three tiers
(Snapdragon 8s Gen 3: 1 + 4 + 3), two tiers with several big cores (4 + 4, Helio G90T 2 + 6),
1 + 3 + 4, a single big core, uniform CPUs, no sysfs values, and every policy (auto/big/next/any).
After homm5/tests/test_render_thread_cores.py; no device."""
from pathlib import Path
import subprocess
import unittest

from tests.host_environment import CLANG, environment

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/tests/render-thread-cores"

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define CPU_SETSIZE 64
typedef uint64_t cpu_set_t;
#define CPU_ZERO(p) (*(p) = 0)
#define CPU_SET(i, p) (*(p) |= UINT64_C(1) << (i))
/* ACTUAL */
static unsigned cases;
static void check(const long *c, int n, uint64_t automatic, uint64_t big, uint64_t next)
{
    const uint64_t expected[] = { automatic, big, next, 0 };
    for (int policy = RT_CORES_AUTO; policy <= RT_CORES_ANY; ++policy) {
        cpu_set_t mask = UINT64_MAX;
        long selected = render_core_mask(c, n, policy, &mask);
        if (mask != expected[policy] || !!selected != (mask != 0)) {
            printf("FAIL table %p policy %d: mask %llx, expected %llx, selected %ld\n", (const void *)c, policy,
                   (unsigned long long)mask, (unsigned long long)expected[policy], selected);
            assert(0);
        }
        for (int i = 0; i < n; ++i) if (mask & (UINT64_C(1) << i)) assert(c[i] == selected);
        ++cases;
    }
}
int main(void)
{
    /* cpu_capacity per CPU, as the kernel numbers them (little first). */
    const long sd8s_gen3[] = { 393, 393, 393, 873, 873, 873, 873, 1024 };   /* 3 x A520, 4 x A720, 1 x X4 */
    const long four_four[] = { 381, 381, 381, 381, 1024, 1024, 1024, 1024 };/* 4 x A53 + 4 x A73 (Adreno 610) */
    const long helio_g90t[] = { 435, 435, 435, 435, 435, 435, 1024, 1024 }; /* 6 x A55 + 2 x A76 */
    const long one_three_four[] = { 400, 400, 400, 400, 800, 800, 800, 1024 };
    const long one_seven[] = { 400, 400, 400, 400, 400, 400, 400, 1024 };
    const long uniform[] = { 1024, 1024, 1024, 1024 };
    const long no_sysfs[] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    const long partly_unknown[] = { 400, 0, 1024 }, missing[] = { 400, -1, 1024 };
    const long four_tiers[] = { 200, 400, 700, 1024 };
    const long unordered[] = { 1024, 400, 400, 1024, 400, 400, 1024, 400 };
    check(sd8s_gen3, 8, 0x78, 0x80, 0x78);        /* three tiers: the middle cores (measured) */
    check(four_four, 8, 0xF0, 0xF0, 0x0F);        /* two tiers, four big cores: the big tier */
    check(helio_g90t, 8, 0xC0, 0xC0, 0x3F);       /* two tiers, two big cores: the big tier */
    check(one_three_four, 8, 0x70, 0x80, 0x70);
    check(one_seven, 8, 0x7F, 0x80, 0x7F);        /* one big core: the next tier */
    check(uniform, 4, 0, 0, 0);
    check(no_sysfs, 8, 0, 0, 0);
    check(NULL, 0, 0, 0, 0);
    check(partly_unknown, 3, 0, 0, 0);
    check(missing, 3, 0, 0, 0);
    check(four_tiers, 4, 0x04, 0x08, 0x04);
    check(unordered, 8, 0x49, 0x49, 0xB6);
    cpu_set_t mask = UINT64_MAX;
    assert(!render_core_mask(uniform, CPU_SETSIZE + 1, RT_CORES_AUTO, &mask) && !mask);
    printf("PASS render-core selection: %u topology/policy cases; one tier, unknown, empty and oversized tables choose no tier\n", cases);
    return 0;
}
'''


def function(text, name):
    start = text.rfind("\n", 0, text.index(name)) + 1
    depth, end = 1, text.index("{", start) + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


class RenderThreadCoresTest(unittest.TestCase):
    def test_topologies_and_policies(self):
        source = (ROOT / "android/host/d3d9_thread.c").read_text(encoding="utf-8")
        start = source.index("enum { RT_CORES_AUTO")
        actual = source[start:source.index(";", start) + 1] + "\n" + function(source, "static long render_core_mask(")
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "probe.c").write_text(HARNESS.replace("/* ACTUAL */", actual), encoding="utf-8")
        env, linker = environment("x64")

        def run(command):
            result = subprocess.run([str(part) for part in command], cwd=OUT, env=env, capture_output=True, timeout=60)
            text = (result.stdout + result.stderr).decode(errors="replace")
            self.assertEqual(result.returncode, 0, text)
            return text

        run([CLANG, "/nologo", "/O2", "/W4", "/WX", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
             "/clang:--target=x86_64-pc-windows-msvc", "/c", "probe.c", "/Foprobe.obj"])
        run([linker, "/nologo", "probe.obj", "/OUT:probe.exe"])
        output = run([OUT / "probe.exe"])
        self.assertIn("PASS render-core selection: 48 topology/policy cases", output)
        # The policy switch and the lifted guest mask when no tier is chosen.
        self.assertIn('getenv("VOID_RT_CORES")', source)
        self.assertIn('"%s/rt-cores", void_data_dir()', source)
        self.assertIn('"[rt] render thread on any core (policy %s%s)"', source)


if __name__ == "__main__":
    unittest.main()
