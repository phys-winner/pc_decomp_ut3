# Third-party notices

The Win32/D3D9 compatibility host in `android/host` and the private ARM64 runtime
were copied and adapted from the local Heroes V port, itself based on the local
The Void port. That project's authored port code is GPL-3.0-or-later; its license
and original notices are retained in `reference/homm5/`. Upstream-derived portions
retain the terms described below. Exact source and copied-file hashes are recorded
in `dependencies-host.json`; the lifter has its separate dependency records.

| Component | Revision/version | License | Retained notice |
| --- | --- | --- | --- |
| pcrecomp runtime and lifting components | `904ac3129d0c9d05efbeec19a1dce6abbe7f5d26` | MIT | `reference/pcrecomp/LICENSE` |
| MojoShader, including marked local shader-compatibility extensions | `cf2661bcb55d9a8732e86f79922592d7efdf5383` | Zlib | `reference/mojoshader/LICENSE.txt` |
| dlmalloc | 2.8.6, relicensed 2023 | MIT-0 | Notice at the beginning of `reference/dlmalloc/malloc.c` |
| Adapted x87 sliding-window algorithm by sistemas3cecasem | NFSU2 revision `62a1d3a17fc7619b9d32df182b4b47d1a4f2e959` | MIT | Original patch and attribution in `reference/homm5/x87-attribution/` |

MojoShader's copied sources already contain the H5/The Void port's marked local
extensions. They are an altered source snapshot and must not be represented as
an unmodified upstream release. This UT3 adaptation removes the H5-specific
guest-address overrides and uses the same general Win32 and graphics mechanisms.

The current host does not include bcdec, etcpak or astcenc. The D3D9 texture decoder
used here is part of the copied compatibility host. Android system libraries,
the NDK, Java, Python and installed Python packages are separate development/runtime
dependencies with their own notices.

Unreal Tournament 3 executable files, DLLs and game data remain the property of
their respective rights holders. These notices do not grant rights to them.
The repository and the APK carry none of the game's executables, DLLs or assets:
the builder reads the user's own Steam installation, `libut3.so` holds native code
statically translated from those executables, and the game makes their PE images
at start from the game data the player supplies.

The launcher in `android/launcher` is the author's launcher base, shared with the
author's other ports and part of this port under its license.

## Microsoft Visual C++ 2005 runtime

The game's DLLs import the Visual C++ 2005 runtime. `msvcr80.dll` and `msvcp80.dll`,
version 8.0.50727.9680 from the build machine's `C:\Windows\WinSxS`, are statically
translated like the game's own modules, and their PE images are linked into
`libut3.so` as data (`android/host/pe_builtin.c`). They are Microsoft's redistributable
runtime files, © Microsoft Corporation, distributed under the terms of the Microsoft
Visual C++ 2005 Redistributable Package.

## Native audio runtime

The ARM64 native audio dependency is OpenAL Soft 1.25.2, using Oboe
1.11.0 for physical AAudio output. Its two dynamic libraries are retained in
`reference/audio-runtime/arm64-v8a/` and staged into `build/native-audio/runtime/`
by `tools/build_native_audio.py`. Exact binary, source, archive, recipe and
license hashes are recorded in `dependencies-audio.json` and the retained
upstream build receipt.

| Component | Revision/version | License | Retained notice |
| --- | --- | --- | --- |
| OpenAL Soft | 1.25.2, `b2c48f7718ef3fcf67921a8b6534c4914e328970` | LGPL-2.0-or-later | `reference/audio-runtime/licenses/OpenAL-COPYING.txt` |
| Oboe | 1.11.0, `b115f47593969fd67a21e9f63640ffef749b5067` | Apache-2.0 | `reference/audio-runtime/licenses/Oboe-LICENSE.txt` |
| LLVM libc++ shared runtime | Android NDK 27.2.12479018 | Apache-2.0 with LLVM exceptions and included notices | `reference/audio-runtime/licenses/LLVM-NDK-NOTICE.txt` |
| fmt bundled with OpenAL Soft | 11.2.0 | MIT | `reference/audio-runtime/licenses/fmt-LICENSE.txt` |
| Microsoft GSL bundled with OpenAL Soft | Snapshot in the pinned OpenAL source tree | MIT | `reference/audio-runtime/licenses/GSL-LICENSE.txt` |
| PFFFT bundled with OpenAL Soft | Snapshot in the pinned OpenAL source tree | BSD-style terms, including FFTPACK attribution | `reference/audio-runtime/licenses/OpenAL-PFFFT-LICENSE.txt` |

The pristine OpenAL Soft 1.25.2 and Oboe 1.11.0 source trees are not part of this
repository: `tools/build_native_audio.py --rebuild` expects them, with their archives
and provenance, in `reference/openal-soft/` and `reference/oboe/` (upstream
<https://github.com/kcat/openal-soft> and <https://github.com/google/oboe> at the
revisions above). The exact build recipe and its authored metrics/fixture sources
are retained in `reference/audio-runtime/rebuild/`. This is an altered native build:
its overlay patches and compiler/configuration are recorded in
`reference/audio-runtime/upstream-build-receipt.json`. It provides AAudio playback,
with capture, EAX, OpenSL, null and wave backends disabled. The original
`FrostpunkAudio_*` instrumentation export names remain in this reused binary. In the
shipped `libopenal.so` the build machine's user name in the embedded NDK header
paths is replaced by equal-length neutral text; no other byte differs.

When packaging these audio DSOs, retain the notices in
`build/native-audio/licenses/`, including the source provenance notice. The
source snapshot and rebuild recipe above correspond to the shipped native
audio dependency; they do not include proprietary Unreal Tournament 3 assets.

## Alternative native DXVK renderer

DXVK Native 2.4.1, revision `0cf05780abd7250c2cd713b7749cf32180157cf5`
(<https://github.com/doitsujin/dxvk>), is shipped as the prebuilt
`reference/dxvk-runtime/libdxvk_d3d9.so`, built by `tools/build_dxvk.py --rebuild`
from that revision with the overlays in that tool; its hash and the hashes of every
upstream source file it was built from are in `dependencies-dxvk.json`. The
repository keeps DXVK's zlib/libpng license `reference/dxvk/LICENSE` and the
headers the port's bridge compiles against (`reference/dxvk/include/native`, the
Vulkan C headers); bundled native DirectX headers retain their LGPL and MinGW-w64
notices, including `reference/dxvk/include/native/directx/COPYING.MinGW-w64.txt`.
The Android WSI derives from the author's Frostpunk port. The UT3-specific x86 COM
boundary and Android build overlays are in `android/dxvk/`,
`android/host/d3d9_dxvk.c`, and `tools/build_dxvk.py`. The native D3D9 DSO is loaded
dynamically and shares the audio dependency's NDK libc++ runtime. In the shipped
library the build machine's user name in the embedded NDK header paths is replaced
by equal-length neutral text; no other byte differs. The APK includes these
retained license notices.

## Turnip and adrenotools

| Component | Revision/version | License | Retained notice |
| --- | --- | --- | --- |
| Mesa Turnip Vulkan driver, community build "Turnip v26.0.0 R8" (K11MCH1/AdrenoToolsDrivers, release `v26.0.0-rc08`), unmodified | Mesa 26.0.0-devel | Mesa: MIT for its core; the full license manifest of this binary build is not reconstructed | `reference/turnip-android/LICENSE-NOTE.txt`, `provenance.json` |
| libadrenotools | `8fae8ce254dfc1344527e05301e43f37dea2df80` | BSD-2-Clause | `reference/adrenotools/LICENSE` |
| linkernsbypass (bundled with libadrenotools) | `aa3975893d83ef1bc84c321ec60c65fbf1287887` | BSD-2-Clause | `reference/adrenotools/lib/linkernsbypass/LICENSE` |

`tools/build_turnip.py` builds libadrenotools and its hooks from these sources with
the NDK and packages the driver as `libut3_turnip.so`. A Turnip driver the player
chooses in the launcher is the player's own file and is not distributed with the port.
