# pc_decomp_ut3

Native Android ARM64 port of **Unreal Tournament 3** (PC, 2007), built by statically recompiling the original
32-bit x86 executable and its DLLs to C, compiling that C for AArch64 with the Android NDK, and supplying an
Android Win32 / Direct3D 9 compatibility host. No x86 emulator runs on the phone.

**Bring your own game.** The port needs the files of the **Steam version** of Unreal Tournament 3:
<https://store.steampowered.com/app/13210/Unreal_Tournament_3/>. This repository contains the port, its build
tools and pinned dependencies. Original executables, DLLs, game assets and the C generated from them are not part
of it; every input is checked against the SHA-256 of the Steam build in `config/inputs.json` and `config/modules.json`.

This is an unofficial fan project, not affiliated with or endorsed by Epic Games or the other rights holders of
Unreal Tournament 3. Unreal Tournament 3 and its content belong to their respective owners.

## Status and features

Current version: **0.1-dev** (versionCode 1). Versions are numbered `0.N-dev`, the versionCode counts builds.

- The original game code runs as statically recompiled native code; the PE images it was recompiled from are
  made at start from the game's own `Binaries` (relocated to the port's bases, size and CRC-32 checked). The
  Visual C++ 2005 runtime the game's DLLs import is built into the port.
- Renderers: **OpenGL ES 3.2** (the port's Direct3D 9 on GLES, every device), **DXVK** on the system Vulkan 1.3
  driver, and **DXVK on Mesa Turnip** for Adreno 6xx/7xx, with the bundled Turnip build or **your own driver
  package** (an adrenotools ZIP with `meta.json`, or the driver `.so`). A driver check in a separate process tells
  which renderer can run on the device.
- Launcher: game files from a folder or a ZIP archive (a ZIP stored without compression plays in place), languages
  (English, Russian, German, Polish), update checks through GitHub releases, cleanup of unused files.
- Touch controls with an analog stick, swipe look, weapon bar selection, vehicle profiles, an editable layout and
  an optional gyroscope; physical gamepads as XInput controller 0 with the original bindings and rumble.
- Hor+ widescreen camera, HUD and menu scaling for phones, an `ANDROID` tab in the original settings menu.
- LAN games between the PC game and the port, both ways.
- Bink movies with sound (intro, loading, campaign), skippable by tap or gamepad.

Known limitations: a stable 60 FPS at the display's native resolution is not reached on the tested phones; online
services of the original game are not available; the port needs Android 11 or newer on 64-bit ARM.

Tested devices: Google Pixel 10a (Mali-G715, Android 17), Google Pixel 10 (PowerVR, Android 17), and a Snapdragon
870 phone (Adreno 650, Android 15) for the Turnip renderers.

## Requirements

- Windows 10 or 11 (x64) with the Steam version of Unreal Tournament 3 installed. The UnrealScript package of the
  port is compiled with the game's own compiler.
- The Microsoft Visual C++ 2005 runtime 8.0.50727.9680 in `C:\Windows\WinSxS` (`msvcr80.dll`, `msvcp80.dll`;
  their hashes are pinned in `config/modules.json`). The recompiler reads them there.
- Python 3.12 or newer with the packages of `requirements.txt`.
- Android SDK with build-tools 36.0.0, platform android-37.0, CMake 3.22.1 and NDK 27.2.12479018; a JDK 17 or newer.

## Build the APK

From the root of a clone:

```powershell
python -m pip install -r requirements.txt
$env:ANDROID_SDK_ROOT = Join-Path $env:LOCALAPPDATA "Android\Sdk"
$env:JAVA_HOME = "C:\Path\To\JDK"
```

If Steam keeps the game elsewhere than `C:\Program Files (x86)\Steam\steamapps\common\Unreal Tournament 3`,
set `source_root` in `config/inputs.json` to that folder.

```powershell
python tools/inspect_inputs.py --verify      # the Steam files match the pinned hashes
python tools/lift_ut3.py                     # C for UT3.exe and its DLLs in generated/
python tools/build_uscript.py                # build/uscript/AndroidPort.u with the game's compiler
python tools/build_android.py --shared --jobs 4
python tools/build_apk.py --library build/android/libut3.so --output build/apk/ut3.apk
```

`build_android.py` first stages the pinned native runtimes: OpenAL Soft with Oboe (`reference/audio-runtime`),
DXVK (`reference/dxvk-runtime`; the port's bridge is compiled from `android/dxvk`) and libadrenotools with the
bundled Turnip driver (`reference/adrenotools`, `reference/turnip-android`). Every prebuilt library is pinned by
SHA-256 in the `dependencies-*.json` files; `tools/build_dxvk.py --rebuild` and `tools/build_native_audio.py
--rebuild` rebuild DXVK and OpenAL from upstream sources at the pinned revisions.

`build_apk.py` signs with the signer configured in `PORT_SIGNING_CONFIG`, else with a local debug key
(`build/android/debug.keystore`). An APK installs as an update only over one with the same signer.

Release APKs are additionally built with profile-guided optimization from a profile recorded on a device and
`-mtune=cortex-a77`; the code is the same.

## Install and supply the game

1. Install the APK (Android 11+, arm64-v8a) and open **Unreal Tournament 3**; the launcher starts.
2. Copy the `Binaries`, `Engine` and `UTGame` folders of the Steam installation to the phone, for example to
   `/sdcard/UT3/game`, or pack them into a ZIP. In the launcher choose **Folder…** or **ZIP archive…**. A ZIP whose
   files are stored without compression (7-Zip: compression level *Store*) can be played straight from the archive.
3. Choose a renderer. On Adreno 6xx/7xx, **DXVK (Turnip)** can use your own Turnip driver: **Own driver…** takes an
   adrenotools driver package or the driver `.so`; the driver check then runs for it, and **Use the port's driver**
   goes back to the bundled one.
4. **Play**. The game's settings, saves and logs are in `Android/data/org.ut3.port/files/userdata`.

## Checks

```powershell
python -m unittest tests.test_pe_images        # PE images made from the Steam files equal the recompiled ones
python tests/test_game_settings.py
python tests/test_touch_controls.py
python android/launcher/tests/run_tests.py     # the launcher base
```

Several other tests in `tests/` run native fixtures on a phone over adb; they read the device from the
`UT3_TEST_ANDROID` environment variable.

## Repository layout

| Path | Contents |
| --- | --- |
| `android/host` | The Win32, Direct3D 9, DirectInput, DirectSound, Winsock and file host in C |
| `android/runtime` | Runtime headers of the recompiled code |
| `android/dxvk` | The Android WSI for DXVK and the port's bridge to it |
| `android/app` | The game activity, touch controls and settings in Java; manifest and icons |
| `android/uscript` | The `AndroidPort` UnrealScript package (the `ANDROID` settings tab) |
| `android/launcher` | The launcher base shared with the author's other ports |
| `android/port.json` | The launcher's description of the port |
| `config` | The pinned original inputs and the recompiler's module list |
| `tools` | Recompiler front end, native and APK builders, research tools |
| `tests` | PC and device tests |
| `reference` | Vendored third-party sources and pinned prebuilt runtimes |

## License and credits

The port is licensed under the GNU General Public License, version 3 or (at your option) any later version; see
`LICENSE`. Its compatibility host derives from the author's Heroes of Might and Magic V and The Void ports.
Third-party components keep their own licenses, listed with their notices in `THIRD_PARTY_NOTICES.md`: pcrecomp,
MojoShader, dlmalloc, DXVK, OpenAL Soft, Oboe, libadrenotools, Mesa Turnip and the Microsoft Visual C++ 2005
runtime, among others.
