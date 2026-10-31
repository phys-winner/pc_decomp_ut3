"""Build the local Android UT3 launcher; game assets remain under /sdcard/UT3."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import ssl
import struct
import subprocess
import sys
import tempfile
import zipfile
from build_android import native_dependencies, sha

ROOT = Path(__file__).resolve().parents[1]
# The launcher base: PORT_LAUNCHER, else android/launcher (vendored, or a git submodule), else a checkout
# beside this project.
LAUNCHER = next((path for path in (Path(os.environ["PORT_LAUNCHER"]) if os.environ.get("PORT_LAUNCHER") else None,
                                   ROOT / "android/launcher", ROOT.parent / "launcher")
                 if path is not None and (path / "tools/launcher.py").is_file()), None)


def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


# The launcher lists assets/native-licenses by file name: name each text after its component.
LICENSE_TITLES = {
    "LICENSE": "DXVK.txt",
    "COPYING.MinGW-w64.txt": "MinGW-w64 headers (DXVK).txt",
    "OpenAL-COPYING.txt": "OpenAL Soft.txt",
    "OpenAL-PFFFT-LICENSE.txt": "PFFFT (OpenAL Soft).txt",
    "fmt-LICENSE.txt": "fmt (OpenAL Soft).txt",
    "GSL-LICENSE.txt": "Microsoft GSL (OpenAL Soft).txt",
    "Oboe-LICENSE.txt": "Oboe.txt",
    "SOURCE-PROVENANCE.txt": "Native audio sources.txt",
    "LLVM-NDK-NOTICE.txt": "LLVM libc++ (Android NDK).txt",
    "adrenotools.BSD-2-Clause.txt": "adrenotools.txt",
    "linkernsbypass.BSD-2-Clause.txt": "linkernsbypass.txt",
    "Turnip-LICENSE-NOTE.txt": "Mesa Turnip.txt",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, help="Use an already built libut3.so")
    parser.add_argument("--output", type=Path, default=ROOT / "build/apk/ut3.apk")
    args = parser.parse_args()
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    tools = sdk / "build-tools/36.0.0"
    android = sdk / "platforms/android-37.0/android.jar"
    java = Path(os.environ["JAVA_HOME"]) / "bin"
    library = args.library or ROOT / "build/android/libut3.so"
    if not args.library:
        run(sys.executable, ROOT / "tools/build_android.py", "--shared")
    library = library.resolve()
    data = library.read_bytes()
    if len(data) < 64 or data[:5] != b"\x7fELF\x02" or struct.unpack_from("<HH", data, 16) != (3, 183):
        parser.error("The APK requires an AArch64 ELF shared library")
    runtime, pinned, licenses = native_dependencies()
    native_pins = {path.relative_to(ROOT).as_posix(): digest for path, digest in pinned.items()}
    built = json.loads(library.with_suffix(".json").read_text())
    if built.get("sha256") != hashlib.sha256(data).hexdigest() or built.get("native_dependencies") != native_pins:
        parser.error("Game library receipt does not match the current native audio/DXVK bundle; rebuild it first")
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    # Release signing: a signer config (PORT_SIGNING_CONFIG, else the maintainer's machine config); without one,
    # a local debug keystore, as a debug build would use. Updates install only over the same signer.
    config = Path(os.environ.get("PORT_SIGNING_CONFIG", Path.home() / ".agents/skills/adapt-arm32-game/machine_config.json"))
    if config.is_file():
        signing = json.loads(config.read_text())["signer"]
        signer_dir = Path(signing["dir"])
        signer = signer_dir / signing["apksigner"]
        certificate = signer_dir / signing["cert"]
        key = signer_dir / signing["key"]
        for required in (signer, certificate, key):
            if not required.is_file():
                parser.error(f"Configured portable signing input is missing: {required}")
        certificate_sha = hashlib.sha256(ssl.PEM_cert_to_DER_cert(certificate.read_text())).hexdigest()
        if certificate_sha != signing["expected_cert_sha256"]:
            parser.error("Configured signing certificate SHA256 mismatch")
        sign = ["--key", key, "--cert", certificate]
    else:
        keystore = ROOT / "build/android/debug.keystore"
        if not keystore.exists():
            keystore.parent.mkdir(parents=True, exist_ok=True)
            run(java / "keytool.exe", "-genkeypair", "-keystore", keystore, "-storepass", "android", "-keypass", "android",
                "-alias", "androiddebugkey", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
                "-dname", "CN=Android Debug,O=Android,C=US")
        signer = tools / "lib/apksigner.jar"
        sign = ["--ks", keystore, "--ks-pass", "pass:android", "--key-pass", "pass:android"]
        certificate_sha = None
    script = ROOT / "build/uscript/AndroidPort.u"
    sources = sorted((ROOT / "android/uscript/AndroidPort/Classes").glob("*.uc"))
    script_receipt = json.loads(script.with_suffix(".json").read_text()) if script.with_suffix(".json").is_file() else {}
    source_pins = {str(path.relative_to(ROOT)): sha(path) for path in sources}
    if (not script.is_file() or sha(script) != script_receipt.get("sha256") or
            source_pins != script_receipt.get("sources") or
            sha(ROOT / "tools/build_uscript.py") != script_receipt.get("tool_sha256")):
        run(sys.executable, ROOT / "tools/build_uscript.py")
        script_receipt = json.loads(script.with_suffix(".json").read_text())
    if sha(script) != script_receipt["sha256"] or any(sha(ROOT / path) != expected for path, expected in script_receipt["sources"].items()):
        parser.error("AndroidPort script package or sources changed during compilation")
    with tempfile.TemporaryDirectory(prefix="ut3-apk-", dir=out.parent) as temporary:
        stage = Path(temporary)
        classes, dex, assets = stage / "classes", stage / "dex", stage / "assets"
        for directory in (classes, dex, assets):
            directory.mkdir()
        for name, source in (("THIRD_PARTY_NOTICES.md", ROOT / "THIRD_PARTY_NOTICES.md"),
                             ("GPL-3.0.txt", ROOT / "reference/homm5/LICENSE"),
                             ("pcrecomp-license.txt", ROOT / "reference/pcrecomp/LICENSE"),
                             ("mojoshader-license.txt", ROOT / "reference/mojoshader/LICENSE.txt"),
                             ("x87-attribution.md", ROOT / "reference/homm5/x87-attribution/README.md")):
            if source.is_file():
                shutil.copy2(source, assets / name)
        native_licenses = assets / "native-licenses"
        native_licenses.mkdir()
        for source in licenses:
            target = native_licenses / LICENSE_TITLES.get(source.name, source.name)
            if target.exists():
                parser.error(f"Two native licenses would share the name {target.name}")
            shutil.copy2(source, target)
        shutil.copy2(script, assets / "AndroidPort.u")
        shutil.copy2(ROOT / "android/uscript/AndroidPort/AndroidPortOptions.ini", assets / "AndroidPortOptions.ini")
        shutil.copy2(ROOT / "android/uscript/AndroidPort/AndroidPortOptions-RUS.ini", assets / "AndroidPortOptions-RUS.ini")
        # The launcher: its sources in this game's package, the manifest with its markers expanded,
        # assets/launcher (port.json, build.json) and the icons of android/app/res.
        if LAUNCHER is None:
            parser.error("The launcher base is missing: add it as android/launcher (git submodule) or beside the project")
        sys.path.insert(0, str(LAUNCHER / "tools"))
        import launcher as port_launcher
        prepared = port_launcher.prepare(ROOT / "android/port.json", ROOT / "android/app/AndroidManifest.xml", stage / "launcher")
        shutil.copytree(Path(prepared["assets"]) / "launcher", assets / "launcher")
        # The launcher's Vulkan driver check (process :probe) behind the DXVK modes.
        probe = port_launcher.build_probe(stage / "probe", prepared["package"]) if prepared["probe"] else None
        resources = stage / "resources.zip"
        run(tools / "aapt2.exe", "compile", "--dir", ROOT / "android/app/res", "-o", resources)
        run(tools / "aapt2.exe", "link", "-I", android, "--manifest", prepared["manifest"],
            "-A", assets, resources, "-o", stage / "base.apk")
        sources = sorted((ROOT / "android/app/src").rglob("*.java")) + [Path(p) for p in prepared["sources"]]
        run(java / "javac.exe", "--release", "8", "-encoding", "UTF-8", "-cp", android, "-d", classes, *sources)
        run(java / "java.exe", "-cp", tools / "lib/d8.jar", "com.android.tools.r8.D8",
            "--lib", android, "--min-api", "30", "--output", dex, *sorted(classes.rglob("*.class")))
        unsigned = stage / "unsigned.apk"
        shutil.copy2(stage / "base.apk", unsigned)
        with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as apk:
            apk.write(dex / "classes.dex", "classes.dex")
            apk.write(library, "lib/arm64-v8a/libut3.so")
            for name, source in sorted(runtime.items()):
                apk.write(source, "lib/arm64-v8a/" + name)
            if probe is not None:
                apk.write(probe, "lib/arm64-v8a/" + probe.name)
            probe_sha = hashlib.sha256(probe.read_bytes()).hexdigest() if probe is not None else None
        run(tools / "zipalign.exe", "-f", "-P", "16", "4", unsigned, stage / "aligned.apk")
        run(java / "java.exe", "-jar", signer, "sign", *sign, "--out", out, stage / "aligned.apk")
        run(java / "java.exe", "-jar", signer, "verify", "--verbose", out)
        run(tools / "zipalign.exe", "-c", "-P", "16", "4", out)
    if any(sha(path) != digest for path, digest in pinned.items()):
        raise ValueError("Native dependencies changed during APK packaging")
    receipt = {"apk": str(out), "sha256": hashlib.sha256(out.read_bytes()).hexdigest(),
               "library": str(library), "library_sha256": hashlib.sha256(data).hexdigest(),
               "package": "org.ut3.port", "native_dependencies": native_pins,
               "signer_certificate_sha256": certificate_sha or "debug keystore build/android/debug.keystore",
               "native_settings_package_sha256": script_receipt["sha256"],
               "renderers": ["gles", "dxvk", "dxvk-turnip"], "audio": "OpenAL Soft / Oboe AAudio",
               "launcher": {"package": prepared["package"], "commit": port_launcher.git_commit(LAUNCHER),
                            "probe_sha256": probe_sha},
               "gameplay": "NOT YET VERIFIED"}
    out.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
