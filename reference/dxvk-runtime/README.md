# DXVK Native D3D9 for UT3 on Android (prebuilt)

`libdxvk_d3d9.so` is [DXVK](https://github.com/doitsujin/dxvk) at revision
`0cf05780abd7250c2cd713b7749cf32180157cf5`, built by `python tools/build_dxvk.py --rebuild --host-tools DIR`
(meson release build, `aarch64-linux-android29`, Android NDK 27.2.12479018) with the overlays listed in
`tools/build_dxvk.py` and the port's Android WSI `android/dxvk/wsi_android.cpp`. Its SHA-256 and the WSI's are
pinned in `dependencies-dxvk.json` (`prebuilt`); the default `tools/build_dxvk.py` only stages this file and
compiles the port's bridge (`android/dxvk/bridge.cpp`) with the NDK.

DXVK is licensed under the zlib license (`reference/dxvk/LICENSE`); its bundled DirectX headers keep their
LGPL-2.1-or-later notices (`reference/dxvk/include/native/directx/COPYING.MinGW-w64.txt`).

To rebuild it, check out that revision into `reference/dxvk`, provide meson and glslang (`DIR/python` with
`mesonbuild`, `DIR/glslang/bin`), run the command above, copy `build/dxvk/libdxvk_d3d9.so` here and update the pin.
