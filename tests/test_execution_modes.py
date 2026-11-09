"""Run the production native-mode loaders/selectors with real legacy files on the host.

Like test_render_thread_cores, this compiles the exact small production functions,
without replacing their policy with a Python model. Kernel bodies and guest ABI
routing remain covered by the native fixtures and test_native_same_unit_calls.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import unittest

from tests.host_environment import CLANG, environment

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/tests/execution-modes"
# Source, cached field, loader, old default, old diagnostic-2 selection.
MODULES = (
    ("physx_native", "native_mode", "physx_native_mode_load", 1, 2),
    ("render_native", "render_mode", "render_mode_load", 1, 2),
    ("game_native", "game_native_mode", "game_native_load_mode", 1, 2),
    ("render_hash_native", "render_hash_mode", "render_hash_mode_load", 1, 2),
    ("render_draw_native", "render_draw_mode", "render_draw_mode_load", 1, 0),
    ("render_visibility_native", "visibility_mode", "visibility_load_mode", 0, 0),
    ("render_sparse_native", "render_sparse_mode", "sparse_load_mode", 0, 0),
    ("render_bitarray_native", "render_bitarray_mode", "bitarray_load_mode", 0, 0),
    ("render_light_native", "render_light_array_mode", "light_array_load_mode", 0, 0),
    ("render_occlusion_native", "render_occlusion_mode", "occlusion_load_mode", 0, 0),
    ("ai_native", "ai_native_mode", "ai_native_mode_load", 1, 0),
    ("ai_reach_native", "ai_reach_mode", "ai_reach_load_mode", 1, 2),
    ("ai_perception_native", "ai_perception_mode", "perception_load_mode", 1, 0),
    ("ai_trace_native", "ai_trace_mode", "ai_trace_load_mode", 1, 2),
)


def function(text, name):
    match = re.search(r"(?m)^static[^\n]*\b" + re.escape(name) + r"\(void\)\s*\{", text)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end] + "\n"


class ExecutionModesTest(unittest.TestCase):
    def test_production_loaders_files_and_process_cache(self):
        OUT.mkdir(parents=True, exist_ok=True)
        source = [r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native_mode.inc"
static const char *data_dir;
static unsigned reports;
static const char *void_data_dir(void) { return data_dir; }
static void void_log(const char *format, ...) { (void)format; ++reports; }
''']
        pins, selectors, fields, files = {}, [], [], []
        for name, field, loader, _, _ in MODULES:
            path = ROOT / "android/host" / (name + ".c")
            text = path.read_text(encoding="utf-8")
            pins[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
            actual = function(text, loader)
            self.assertIn('#include "native_mode.inc"', text, name)
            self.assertIn("void_native_mode(", actual, name)
            flag = re.search(r'"%s/([a-z-]+)"', actual)[1]
            files.append(flag)
            source.append("static int " + field + " = -1;\n" + actual)
            # PhysX caches the field in a local before selecting. The other
            # modules embed this same selector at their wrapper boundary.
            if name == "physx_native":
                source.append(function(text, "physx_native_mode"))
                selectors.append("physx_native_mode")
            else:
                expression = re.search(r"__builtin_expect\(\s*" + field +
                    r"\s*>=\s*0\s*,\s*1\s*\)\s*\?\s*" + field +
                    r"\s*:\s*" + loader + r"\(\)", text)
                self.assertIsNotNone(expression, name)
                selector = "select_" + name
                source.append("static int " + selector + "(void) { return " + expression[0] + "; }\n")
                selectors.append(selector)
            fields.append("&" + field)
        source.append("static int (*selectors[])(void) = {" + ",".join(selectors) + "};\n")
        source.append("static int *fields[] = {" + ",".join(fields) + "};\n")
        source.append("static const char *files[] = {" + ",".join(json.dumps(f) for f in files) + "};\n")
        source.append(r'''
int main(int argc, char **argv)
{
    enum { count = sizeof selectors / sizeof *selectors };
    assert(argc == count + 2);
    data_dir = argv[1];
    const char *mode = getenv("UT3_EXECUTION_MODE");
    int exact = mode && !strcmp(mode, "exact");
    for (int requested = 0; requested <= 2; ++requested)
        assert(void_native_mode(requested) == (exact ? 0 : requested));
    for (unsigned i = 0; i < count; ++i) {
        int expected = atoi(argv[i + 2]);
        assert(*fields[i] == -1);
        assert(selectors[i]() == expected && *fields[i] == expected);
        assert(reports == i + 1);
    }
    /* Saving a different setting/file while the process runs must not switch
     * modules already selected. Each harness invocation is a fresh process. */
    assert(!_putenv_s("UT3_EXECUTION_MODE", exact ? "optimal" : "exact"));
    for (unsigned i = 0; i < count; ++i) {
        char path[1100];
        assert(snprintf(path, sizeof path, "%s/%s", data_dir, files[i]) < (int)sizeof path);
        FILE *file = fopen(path, "wb");
        assert(file && fputc('1', file) == '1' && !fclose(file));
        assert(selectors[i]() == atoi(argv[i + 2]));
        assert(reports == count);
    }
    printf("PASS %u production mode loaders: legacy files, exact priority, cached selection\n", count);
    return 0;
}
''')
        probe = OUT / "probe.c"
        probe.write_text("\n".join(source), encoding="utf-8")
        helper = ROOT / "android/host/native_mode.inc"
        pins[str(helper.relative_to(ROOT))] = hashlib.sha256(helper.read_bytes()).hexdigest()
        env, linker = environment("x64")
        env.pop("UT3_EXECUTION_MODE", None)
        commands, results = [], []

        def run(command, process_env=env):
            command = [str(part) for part in command]
            commands.append(command)
            result = subprocess.run(command, cwd=OUT, env=process_env, capture_output=True, timeout=60)
            output = (result.stdout + result.stderr).decode(errors="replace")
            self.assertEqual(result.returncode, 0, output)
            return output

        run([CLANG, "/nologo", "/O2", "/W4", "/WX", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
             "/clang:--target=x86_64-pc-windows-msvc", "/I" + str(helper.parent),
             "/c", probe, "/Foprobe.obj"])
        run([linker, "/nologo", "probe.obj", "/OUT:probe.exe"])
        directory = OUT / "userdata"
        directory.mkdir(exist_ok=True)
        for execution in (None, "optimal", "invalid", "exact"):
            for legacy in (None, "0", "1", "2"):
                for flag in files:
                    path = directory / flag
                    if legacy is None:
                        path.unlink(missing_ok=True)
                    else:
                        path.write_text(legacy, encoding="ascii")
                expected = [0 if execution == "exact" else default if legacy is None else
                            diagnostic if legacy == "2" else int(legacy)
                            for _, _, _, default, diagnostic in MODULES]
                process_env = dict(env)
                if execution is not None:
                    process_env["UT3_EXECUTION_MODE"] = execution
                output = run([OUT / "probe.exe", directory, *expected], process_env)
                self.assertIn("PASS 14 production mode loaders", output)
                results.append({"execution": execution, "legacy": legacy, "expected": expected, "stdout": output})
        pins[str(probe.relative_to(ROOT))] = hashlib.sha256(probe.read_bytes()).hexdigest()
        (OUT / "receipt.json").write_text(json.dumps({"status": "PASS", "modules": len(MODULES),
            "selection_cases": len(results) * len(MODULES), "fresh_processes": len(results),
            "commands": commands, "sha256": pins, "results": results,
            "scope": "Exact production cold loaders and cached selector expressions on Windows host; not ARM kernel or multiplayer validation"},
            indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
