"""HOST-run the DXVK pipeline cache file names and pruning of android/dxvk/pipeline_cache.inc.

The plain C++17 part (namespace pipecache_files) is taken from the overlay as it is, compiled with MSVC
and run on temporary folders holding the files of several driver identities: the current file and the
newest other one of the same vendor/device/driverID stay, older ones of that driver, names of the
format before the driverID, and .part files go; other drivers, other executables and anything not
named exactly like a cache file stay. No device."""
import os
from pathlib import Path
import shutil
import subprocess
import time
import unittest

from tests.host_environment import environment

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/tests/dxvk-pipeline-cache-prune"

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace dxvk {
/* ACTUAL */
}

using namespace dxvk::pipecache_files;

int main(int argc, char** argv) {
  if (argc == 2 && !std::strcmp(argv[1], "names")) {
    std::string name = format("game", 0x5143, 0x43050a01, 18, 0x802fa029, 0x0123456789abcdefull);
    assert(name == "game.5143-43050a01-d18-802fa029-0123456789abcdef.vkpipelinecache");
    Name n;
    assert(parse("game", name, &n) && n.hasDriverId && !n.partial && n.vendor == 0x5143 && n.device == 0x43050a01
        && n.driverId == 18 && n.version == 0x802fa029 && n.uuid == 0x0123456789abcdefull);
    assert(parse("game", name + ".part", &n) && n.partial && n.driverId == 18);
    assert(parse("game", "game.5143-43050a01-802fa029-0123456789abcdef.vkpipelinecache", &n) && !n.hasDriverId);
    assert(!parse("game", "games.5143-43050a01-d18-802fa029-0123456789abcdef.vkpipelinecache", &n));
    assert(!parse("game", "game.5143-43050A01-d18-802fa029-0123456789abcdef.vkpipelinecache", &n));
    assert(!parse("game", "game.0x5143-43050a01-d18-802fa029-0123456789abcdef.vkpipelinecache", &n));
    assert(!parse("game", "game.5143-43050a01-d18-802fa029-0123456789abcdef.vkpipelinecache.tmp", &n));
    assert(!parse("game", "game.dxvk-cache", &n) && !parse("game", "game.vkpipelinecache", &n));
    std::puts("PASS names");
    return 0;
  }
  if (argc != 7)
    return 2;
  uint64_t freed = 0;
  uint32_t removed = prune(argv[1], argv[2], argv[3], uint32_t(std::strtoul(argv[4], nullptr, 16)),
    uint32_t(std::strtoul(argv[5], nullptr, 16)), uint32_t(std::strtoul(argv[6], nullptr, 10)), &freed);
  std::printf("removed %u freed %llu\n", removed, (unsigned long long)freed);
  return 0;
}
'''

SYSTEM = "game.5143-43050a01-d8-{:08x}-{}.vkpipelinecache"
TURNIP = "game.5143-43050a01-d18-{:08x}-{}.vkpipelinecache"


def block(text, opening):
    start = text.index(opening)
    depth, end = 1, text.index("{", start) + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


class PipelineCachePruneTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "android/dxvk/pipeline_cache.inc").read_text(encoding="utf-8")
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "probe.cpp").write_text(HARNESS.replace("/* ACTUAL */", block(source, "  namespace pipecache_files {")),
                                       encoding="utf-8")
        cls.env, linker = environment("x64")
        for command in ([linker.parent / "cl.exe", "/nologo", "/O2", "/std:c++17", "/EHsc", "/W4", "/WX",
                         "/D_CRT_SECURE_NO_WARNINGS", "/c", "probe.cpp", "/Foprobe.obj"],
                        [linker, "/nologo", "probe.obj", "/OUT:probe.exe"]):
            result = subprocess.run([str(part) for part in command], cwd=OUT, env=cls.env, capture_output=True, timeout=300)
            assert result.returncode == 0, (result.stdout + result.stderr).decode(errors="replace")

    def prune(self, folder, current, vendor="5143", device="43050a01", driver="8"):
        result = subprocess.run([str(OUT / "probe.exe"), str(folder), "game", current, vendor, device, driver],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.split()

    def folder(self, name, files):
        """files: {name: (bytes, age in seconds)}; a name ending in / is a folder."""
        folder = OUT / name
        if folder.exists():
            shutil.rmtree(folder)
        folder.mkdir(parents=True)
        now = time.time()
        for file, (size, age) in files.items():
            if file.endswith("/"):
                (folder / file).mkdir()
                continue
            (folder / file).write_bytes(b"\0" * size)
            os.utime(folder / file, (now - age, now - age))
        return folder

    def test_names(self):
        result = subprocess.run([str(OUT / "probe.exe"), "names"], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS names", result.stdout)

    def test_system_driver_keeps_newest_other_and_other_drivers(self):
        current = SYSTEM.format(0x802FA029, "1" * 16)
        keep = {
            current: (100, 10),
            SYSTEM.format(0x80200000, "2" * 16): (10, 100),       # the newest other version of this driver
            TURNIP.format(0x01000001, "4" * 16): (20, 50),        # another driver: its own run prunes it
            TURNIP.format(0x01000002, "5" * 16): (20, 60),
            TURNIP.format(0x01000003, "6" * 16): (20, 70),
            "game.5143-43050a02-d8-802fa029-1111111111111111.vkpipelinecache": (5, 10),   # another device
            "other.5143-43050a01-d8-80100000-3333333333333333.vkpipelinecache": (5, 999), # another executable
            "game.dxvk-cache": (5, 999),
            "game.garbage.vkpipelinecache": (5, 999),
            # Not canonical (uppercase hex; a version of its own: Windows and Android storage ignore case).
            "game.5143-43050A01-d8-80300000-3333333333333333.vkpipelinecache": (5, 999),
            "game.5143-43050a01-d8-80000000-7777777777777777.vkpipelinecache/": (0, 0),   # a folder
        }
        stale = {
            SYSTEM.format(0x80100000, "3" * 16): (1000, 200),     # older than the newest other one
            SYSTEM.format(0x80000000, "8" * 16): (1000, 300),
            "game.5143-43050a01-802fa029-1111111111111111.vkpipelinecache": (1000, 5),   # the earlier name format
            TURNIP.format(0x01000004, "9" * 16) + ".part": (1000, 5),                    # an unfinished write
            "game.5143-43050a01-802fa029-1111111111111111.vkpipelinecache.part": (1000, 5),
        }
        folder = self.folder("system", {**keep, **stale})
        self.assertEqual(self.prune(folder, current), ["removed", "5", "freed", "5000"])
        self.assertEqual(sorted(path.name + ("/" if path.is_dir() else "") for path in folder.iterdir()), sorted(keep))

    def test_new_driver_version_before_its_first_save(self):
        newest, older = SYSTEM.format(0x80200000, "2" * 16), SYSTEM.format(0x80100000, "3" * 16)
        folder = self.folder("first-run", {newest: (10, 100), older: (10, 200)})
        self.assertEqual(self.prune(folder, SYSTEM.format(0x802FA029, "1" * 16)), ["removed", "1", "freed", "10"])
        self.assertEqual([path.name for path in folder.iterdir()], [newest])

    def test_built_in_and_own_turnip_keep_both(self):
        built_in, own, old = (TURNIP.format(version, digit * 16) for version, digit in
                              ((0x01000001, "a"), (0x01000002, "b"), (0x01000003, "c")))
        system = SYSTEM.format(0x802FA029, "1" * 16)
        folder = self.folder("turnip", {built_in: (30, 5), own: (30, 50), old: (30, 500), system: (30, 999)})
        self.assertEqual(self.prune(folder, built_in, driver="18"), ["removed", "1", "freed", "30"])
        self.assertEqual(sorted(path.name for path in folder.iterdir()), sorted([built_in, own, system]))
        # Back on the own Turnip: the built-in one is now the newest other file and stays.
        self.assertEqual(self.prune(folder, own, driver="18"), ["removed", "0", "freed", "0"])

    def test_missing_folder(self):
        self.assertEqual(self.prune(OUT / "does-not-exist", SYSTEM.format(1, "1" * 16)), ["removed", "0", "freed", "0"])


if __name__ == "__main__":
    unittest.main()
