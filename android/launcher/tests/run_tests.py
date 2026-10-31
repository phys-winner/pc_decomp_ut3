"""The launcher base's checks on the PC.

    python tests/run_tests.py

1. LauncherSelfcheck (Java): archives, import plans and jobs, the stored-archive index, discovery,
   cleanup and the text table, against fixtures written here (compiled against the newest
   android.jar of the SDK; only plain-Java code runs).
2. native/zipmount.c against the index the Java check wrote (zig cc or a system C compiler), and a
   warning-free NDK compile for Android when the NDK is installed.
3. tools/launcher.py: every example port.json validates, sources render into a game's package and
   the manifest markers expand.
"""
import json
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import launcher  # noqa: E402


def sdk():
    for name in ("ANDROID_HOME", "ANDROID_SDK_ROOT", "ANDROID_SDK"):
        if os.environ.get(name) and Path(os.environ[name]).is_dir():
            return Path(os.environ[name])
    return Path.home() / "AppData/Local/Android/Sdk"


def android_jar():
    platforms = [p for p in (sdk() / "platforms").glob("android-*") if (p / "android.jar").is_file()]
    if not platforms:
        raise SystemExit("No Android platform in " + str(sdk() / "platforms"))
    def version(p):
        return [int(x) for x in re.findall(r"\d+", p.name)]
    return max(platforms, key=version) / "android.jar"


def java_tool(name):
    home = os.environ.get("JAVA_HOME")
    for candidate in ([Path(home) / "bin" / (name + ".exe"), Path(home) / "bin" / name] if home else []):
        if candidate.is_file():
            return str(candidate)
    found = shutil.which(name)
    if not found:
        raise SystemExit(f"{name} not found: set JAVA_HOME")
    return found


def zip64_archive(path, files):
    """A stored ZIP whose central directory uses ZIP64 records throughout (sizes and offsets
    0xFFFFFFFF with the ZIP64 extra field, a ZIP64 end record and locator)."""
    out, central = bytearray(), bytearray()
    for name, data in files:
        raw = name.encode()
        crc = zlib.crc32(data)
        offset = len(out)
        extra = struct.pack("<HHQQ", 1, 16, len(data), len(data))
        out += struct.pack("<IHHHHHIIIHH", 0x04034B50, 45, 0, 0, 0, 0x21, crc, 0xFFFFFFFF, 0xFFFFFFFF, len(raw), len(extra))
        out += raw + extra + data
        cextra = struct.pack("<HHQQQ", 1, 24, len(data), len(data), offset)
        central += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 45, 45, 0, 0, 0, 0x21, crc, 0xFFFFFFFF, 0xFFFFFFFF,
                               len(raw), len(cextra), 0, 0, 0, 0, 0xFFFFFFFF)
        central += raw + cextra
    start = len(out)
    out += central
    end64 = len(out)
    out += struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0, len(files), len(files), len(central), start)
    out += struct.pack("<IIQI", 0x07064B50, 0, end64, 1)
    out += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 0xFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)
    Path(path).write_bytes(bytes(out))


def fixtures(folder):
    rng = random.Random(5)
    noise = lambda n: bytes(rng.getrandbits(8) for _ in range(n))
    with zipfile.ZipFile(folder / "game.zip", "w") as z:
        z.writestr(zipfile.ZipInfo("UT3 Copy/UTGame/CookedPC/Maps/DM-Deck.ut3"), noise(100000), zipfile.ZIP_STORED)
        z.writestr("UT3 Copy/UTGame/Config/UTGame.ini", "[Engine]\n" * 200, zipfile.ZIP_DEFLATED)
        z.writestr("UT3 Copy/Engine/Config/BaseEngine.ini", "[Core]\n" * 200, zipfile.ZIP_DEFLATED)
        z.writestr("UT3 Copy/directx9c/dxsetup.exe", noise(3000), zipfile.ZIP_STORED)
        z.writestr("images/UT3.exe", noise(5000), zipfile.ZIP_STORED)
        z.writestr("images/PhysXCore.dll", noise(4000), zipfile.ZIP_STORED)
        z.writestr("readme.txt", "hello", zipfile.ZIP_DEFLATED)
    zip64_archive(folder / "zip64.zip", [("big.bin", noise(70000)), ("small.txt", b"zip64")])
    with zipfile.ZipFile(folder / "unsafe.zip", "w") as z:
        z.writestr("UT3/UTGame/CookedPC/a.upk", b"x")
        z.writestr("../evil.txt", b"x")
    with zipfile.ZipFile(folder / "stored.zip", "w") as z:
        z.writestr("data/big.bin", noise(300000), zipfile.ZIP_STORED)
        z.writestr("data/small.txt", b"small file", zipfile.ZIP_STORED)
        z.writestr("data/sub/x.bin", noise(1000), zipfile.ZIP_STORED)
        z.writestr("packed.txt", "compressed " * 100, zipfile.ZIP_DEFLATED)
    # Several distributions: an add-on installed over the original game (one folder holds both, as
    # Heroes V's Hammers of Fate over the original) and a standalone one beside it.
    with zipfile.ZipFile(folder / "editions.zip", "w") as z:
        for name in ("Games/Gold/bin/game.exe", "Games/Gold/data/base.pak", "Games/Gold/data/patch.pak",
                     "Games/Gold/bin2/game.exe", "Games/Gold/data2/addon.pak", "Games/Gold/Manual.pdf",
                     "Games/Tribes/bin/game.exe", "Games/Tribes/data/base.pak", "Games/Tribes/data/tribes.pak"):
            z.writestr(name, name.encode() * 50, zipfile.ZIP_DEFLATED)


def java_checks(folder):
    classes = folder / "classes"
    classes.mkdir()
    jar = android_jar()
    sources = sorted((ROOT / "android/src/port/launcher").glob("*.java")) + [ROOT / "tests/LauncherSelfcheck.java"]
    subprocess.run([java_tool("javac"), "--release", "8", "-Xlint:-options", "-encoding", "UTF-8", "-cp", str(jar),
                    "-d", str(classes), *map(str, sources)], check=True)
    result = subprocess.run([java_tool("java"), "-Xmx256m", "-cp", os.pathsep.join([str(classes), str(jar)]),
                             "port.launcher.LauncherSelfcheck", str(folder)])
    return result.returncode == 0


def c_compiler():
    zig = shutil.which("zig")
    if zig:
        return [zig, "cc"]
    for name in ("cc", "gcc", "clang"):
        if shutil.which(name):
            return [shutil.which(name)]
    return None


def native_checks(folder):
    ok = True
    compiler = c_compiler()
    if compiler:
        exe = folder / ("zipmount_test.exe" if os.name == "nt" else "zipmount_test")
        subprocess.run([*compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-o", str(exe),
                        str(ROOT / "tests/zipmount_test.c"), str(ROOT / "native/zipmount.c")], check=True)
        ok &= subprocess.run([str(exe), "stored.zip", "stored.zmi"], cwd=folder).returncode == 0
    else:
        print("skipped: no C compiler for the zipmount test")
    ndk = sorted((sdk() / "ndk").glob("*"))
    clang = ndk[-1] / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe" if ndk else None
    if clang and clang.is_file():
        for source in ("zipmount.c", "vulkan_probe.c"):
            subprocess.run([str(clang), "--target=aarch64-linux-android30", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-c", str(ROOT / "native" / source), "-o", str(folder / (source + ".o"))], check=True)
        library = launcher.build_probe(folder / "lib", "org.example.port.launcher", ndk[-1])
        data = library.read_bytes()
        ok &= data[:4] == b"\x7fELF" and b"org/example/port/launcher/Probe" in data and b"JNI_OnLoad" in data
        print("NDK: native sources compile for aarch64 without warnings; libportprobe.so registers the game's Probe class")
    else:
        print("skipped: no NDK for the aarch64 compile and libportprobe.so")
    return ok


def tool_checks(folder):
    for example in sorted((ROOT / "examples").glob("*.port.json")):
        launcher.validate(json.loads(example.read_text(encoding="utf-8")))
    spec = json.loads((ROOT / "examples/homm5.port.json").read_text(encoding="utf-8"))
    manifest = folder / "AndroidManifest.xml"
    manifest.write_text("""<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="org.example.port"
    android:versionCode="30110" android:versionName="0.3.1-dev">
  <uses-sdk android:minSdkVersion="30" android:targetSdkVersion="37" />
  <uses-permission android:name="android.permission.INTERNET" />
  <!-- launcher:manifest -->
  <application android:label="Example">
    <!-- launcher:application -->
    <activity android:name="org.example.port.GameActivity" android:exported="true" />
  </application>
</manifest>
""", encoding="utf-8")
    result = launcher.prepare(ROOT / "examples/homm5.port.json", manifest, folder / "prepared")
    text = Path(result["manifest"]).read_text(encoding="utf-8")
    assert result["package"] == "org.example.port.launcher", result["package"]
    assert 'android:name="org.example.port.launcher.LauncherActivity"' in text
    assert text.count("android.permission.INTERNET") == 1, "a permission the game declares is not repeated"
    assert "<!-- launcher:" not in text and "<queries>" in text
    assert 'android:name="org.example.port.launcher.Probe"' in text and 'android:process=":probe"' in text, \
        "a port with DXVK modes gets the driver check in its own process"
    assert result["probe"] is True
    assert "requestLegacyExternalStorage" not in text and "WRITE_EXTERNAL_STORAGE" not in text, \
        "minSdk 30: no storage permissions of Android 8-10"
    legacy = folder / "LegacyManifest.xml"
    legacy.write_text(manifest.read_text(encoding="utf-8").replace('android:minSdkVersion="30"', 'android:minSdkVersion="26"'),
                      encoding="utf-8")
    old = Path(launcher.prepare(ROOT / "examples/homm5.port.json", legacy, folder / "prepared-legacy")["manifest"]).read_text(encoding="utf-8")
    for name in ("READ_EXTERNAL_STORAGE", "WRITE_EXTERNAL_STORAGE"):
        assert f'<uses-permission android:name="android.permission.{name}" android:maxSdkVersion="29" />' in old, \
            "Android 8-10 ask for the storage permissions the manifest declares"
    assert old.count("requestLegacyExternalStorage") == 1 and '<application android:requestLegacyExternalStorage="true"' in old, \
        "Android 10 reads game files by path in the legacy storage view"
    rendered = Path(result["sources"][0]).read_text(encoding="utf-8")
    assert rendered.startswith("package org.example.port.launcher;"), rendered[:60]
    assert json.loads((Path(result["assets"]) / "launcher/build.json").read_text())["versionCode"] == 30110
    broken = dict(spec, distributions=[dict(spec["distributions"][0], renderers=["metal"])])
    turnip = [dict(m) for m in spec["renderers"]["modes"]]
    for m in turnip:
        m.pop("driver", None)
    licenses = dict(spec["app"], licenses=[{"title": "no path"}])
    for wrong, why in ((broken, "an unknown renderer"), (dict(spec, renderers=dict(spec["renderers"], modes=turnip)), "a Turnip mode without its driver"),
                       (dict(spec, app=licenses), "a license entry without a path")):
        try:
            launcher.validate(wrong)
            raise AssertionError(why + " must be refused")
        except launcher.SpecError:
            pass
    launcher.validate(dict(spec, app=dict(spec["app"], licenses=["LICENSE.txt", {"path": "native-licenses/", "title": "Native"}])))
    print("tools: port.json validation, sources and manifest expansion work")
    return True


def main():
    with tempfile.TemporaryDirectory(prefix="launcher-tests-") as temporary:
        folder = Path(temporary)
        fixtures(folder)
        ok = java_checks(folder)
        ok &= native_checks(folder)
        ok &= tool_checks(folder)
    print("ALL PASSED" if ok else "FAILURES")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
