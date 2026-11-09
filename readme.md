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

Current version: **0.5-dev** (versionCode 9). The versionCode also counts internal test builds.

- The original game code runs as statically recompiled native code; the PE images it was recompiled from are
  made at start from the game's own `Binaries` (relocated to the port's bases, size and CRC-32 checked). The
  Visual C++ 2005 runtime the game's DLLs import is built into the port.
- Renderers: **OpenGL ES 3.2** (the port's Direct3D 9 on GLES, every device), **DXVK** on the system Vulkan 1.3
  driver, and **DXVK on Mesa Turnip** for Adreno 6xx/7xx, with the bundled Turnip build or **your own driver
  package** (an adrenotools ZIP with `meta.json`, or the driver `.so`). A driver check in a separate process tells
  which renderer can run on the device.
- GLES rendering runs on a dedicated thread by default, with CPU core selection, asynchronous GPU query
  polling, streaming uploads and shader program prelinking. Program binaries are cached with driver-change
  invalidation; DXVK pipeline caches distinguish drivers and retain the current and newest other cache for
  each driver. DXVK also supports scaled vertex formats through a shader fallback when needed, and hands out
  occlusion queries from one query pool at a time: PowerVR's Vulkan driver splits a render (storing and reloading
  the whole frame) whenever consecutive queries use different pools.
- An optional FPS counter (launcher → Options), drawn the same way with every renderer: the frame rate and a
  frame-time graph in the style of DXVK's HUD, optionally with the system: SoC and its cluster clocks, the game's
  CPU load and busiest threads, GPU and driver, memory, Android's thermal status and headroom, battery and
  screen. Android gives apps no CPU or GPU temperature, so heat shows as the thermal status and the battery's
  temperature.
- Launcher: game files from a folder or a ZIP archive (a ZIP stored without compression plays in place), languages
  (English, Russian, German, Polish), update checks through GitHub releases, cleanup of unused files.
- Touch controls with an analog stick, swipe look, weapon bar selection, vehicle profiles, an editable layout and
  an optional gyroscope; physical gamepads as XInput controller 0 with the original bindings and rumble.
- Optional ASTC/ETC2 texture conversion in the launcher, with an undo action and runtime DXT decoding for
  cache misses and modded textures, including games played directly from a stored ZIP. The launcher shows
  the scan while it runs and offers retry after a read failure. Original game packages are retained.
- Drag, resize and hide individual touch controls; floating or fixed movement stick, movement while holding
  left fire, swipe/tap weapon selection and separate weapon bar/wheel sizes. Global touch visibility includes
  a startup suggestion based on connected input devices and a “don't ask again” choice.
  Hold the on-screen Pause button to open the layout editor; a short tap opens the original pause menu.
- Hor+ widescreen camera, HUD and menu scaling including button callouts, tabs, game modes and map lists;
  an `ANDROID` tab in the original settings menu. Touching an option preserves the list's scroll position.
- Touch selection in the original command menu, including categories, commands and recipients. The Scorpion
  uses direct stick steering with an axial dead zone, so a small sideways drift of the movement finger does
  not lock its wheels. Throttle remains analog, and physical gamepad axes retain their original behavior.
- Tapping an editable text field opens an Android text-editing dialog after the original UI consumes the
  click. The keyboard control also edits console and chat lines; commits preserve the original character
  filters, and cancelling keeps the original text.
  Tabs and bottom action buttons have taller touch targets, with gaps assigned to the nearest button.
  Long tab captions fit their width; the Android page's button caption uses the selected localization.
  Seat switching is hidden in one-seat vehicles; its wheel shows available seats and permitted bot swaps.
- The first start uses an installed localization matching the system language, with English as fallback;
  the FPS selector lists the display's supported refresh rates at its current resolution.
- The in-game Android settings support English, Russian, Czech, German, Spanish, French, Hungarian,
  Italian and Polish. Weapon-button swipes and weapon-bar swipes can be configured separately.
- Every finger turns the camera by its own movement: a held Fire button (also a phone's shoulder trigger
  mapped onto it) never blocks look drags with another finger, and dragging from Fire, secondary fire, Jump or
  Crouch turns the camera too, so the thumb that fired or jumped keeps aiming.
- LAN games between the PC game and the port, both ways.
- OpenSpy service and authentication compatibility, adapted from `mods/fix_mp/dinput8.dll` to the pinned Steam
  executable. The original login/profile menu is available; an OpenSpy account is needed for online play.
- Profile writes before sign-in are rejected, so startup defaults cannot overwrite saved settings or campaign
  progress. ZIP installations create physical save directories beneath virtual archive parents as needed.
- Bink movies with sound (intro, loading, campaign), skippable by tap or gamepad.
- The original PC hardware appraisal is disabled on Android. The advanced-video **MAX GRAPHICS** button
  applies the original highest quality presets, then disables motion blur, depth of field and both bloom
  options. The original **Screen Percentage** is the only 3D resolution control and survives restarts.
- **PhysX effects (CPU)** enables the original hardware-effect path with the game's PhysX 2.7.3 CPU solver:
  hardware scene, cloth and fluid descriptors are translated at the SDK boundary. No AGEIA PPU or CUDA GPU is
  emulated. **OpenAL (AAudio / EAX)** selects native OpenAL Soft with EAX 1–5 and EFX effects; **Generic Software**
  is the basic mode. Android processes the physics and audio effects on the CPU: on the tested Dimensity tablet
  OpenAL with EAX gave about 20% fewer frames per second in the benchmark demo (29.2 against 23.4). The PhysX
  option is the profile setting the game applies on every map start; the CPU simulates scenes, cloth and fluids
  in both settings, and with it on DM-HeatRay_PhysX ran about 6% slower on a Helio G90T. Maps made for PhysX
  hardware are CPU-heavy either way: DM-HeatRay_PhysX runs at about 10–11 fps on that phone (2.1 before the
  native fluid kernels, x87 values in C locals and the native pair removal of the second PhysX thread).
- Android settings include **FSR1 EASU/RCAS** and **FXAA**. Spatial filtering runs on the 3D scene
  before the HUD, using the original Screen Percentage; both work with GLES and DXVK.
- Gyroscope mode **Firing or aiming** follows weapon firing, sniper zoom and guided Redeemer control without
  holding a separate activation key. Existing gyroscope modes retain their saved indices.
- Native crashes and uncaught Java exceptions are journaled in `userdata/ut3-crash.txt` next to `userdata/ut3.log`.
  With the launcher's **Crash monitor** option (Options, off by default) a separate Android process (about 30 MB
  of memory for the whole game) shows the cause and report path after a crash, with **Copy to clipboard** and
  **Exit** buttons, and keeps persistent reports in `userdata/crashes`: the native journal, game-log tails and
  Android's available exit trace/tombstone.

Known limitations: a stable 60 FPS at the display's native resolution is not reached on the tested phones;
the port needs Android 11 or newer on 64-bit ARM. OpenSpy endpoint initialization, protocol boundaries and an
authenticated Internet match on the Ural BTFA server have been checked. Account creation and every server/mod
combination have not been verified.

Tested devices: Google Pixel 10a (Mali-G715, Android 17), Google Pixel 10 (PowerVR, Android 17), and a Snapdragon
870 phone (Adreno 650, Android 15) for the Turnip renderers. Earlier GLES, texture-cache, menu and touch
checks also ran on a BOOX GALILEO3 tablet with Android 11. The 0.3-dev changes were additionally checked on
realme GT Neo 3T (Snapdragon 870, Android 15) and realme RMX3851 (Snapdragon 8s Gen 3, Android 16).
The final version-bumped APK passed build, signature and alignment checks; it was not installed for a
separate device run. The scaled-vertex fallback still needs testing on a proprietary Qualcomm Vulkan 1.3
driver, and render-thread core selection needs a two-cluster device comparison.

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
git submodule update --init -- reference/astcenc reference/etcpak
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

`tools/build_postfx_shaders.py` regenerates the checked-in Shader Model 3 FSR1/FXAA kernels using
Windows' D3DCompiler.

To prepare the PhysX Mod Pack without running its MSI installer:

```powershell
python tools/extract_physx_mod.py mods/UT3_PhysXModPack.msi mods/UT3_PhysXModPack
```

Copy the extracted `game` directory's contents into the game root. The tool verifies all 73 MSI files and
writes a SHA-256 manifest and installation notes; original documents and licences are retained separately.
On the tested Android device, HeatRay's hardware-requested fluids and Tornado's hardware-requested cloth
create successfully with the CPU solver.

Shaders that no shader cache holds, such as the materials of this pack or of any other mod, are compiled on
the device: `android/host/hlsl_vkd3d.c` answers the game's `D3DXCompileShader` with vkd3d-shader
(`reference/vkd3d`, built by `tools/build_vkd3d.py`), and the results are kept in `<data>/shadercache` while
the game also stores them in its own local shader cache. The first load of a map with new materials takes
longer (on the tested Dimensity tablet about 17 ms per shader, 1100 shaders for DM-HeatRay_PhysX and 5600
for CTF-Tornado_PhysX); later loads read the caches. `tools/test_vkd3d.py` checks the compiler on a phone
against bytecode of Microsoft's `d3dx9_35.dll` made from recorded requests (`tools/compile_shader_requests.py`).

The optional converter, `libtexconv.so`, is built with `tools/build_texconv.py` from the upstream ASTC/ETC2
submodules in `reference/astcenc` and `reference/etcpak`. Their revisions are pinned by Git; source hashes in
`dependencies-textures.json` use LF line endings so validation also accepts Windows CRLF checkouts.

`build_apk.py` signs with the signer configured in `PORT_SIGNING_CONFIG`, else with a local debug key
(`build/android/debug.keystore`). An APK installs as an update only over one with the same signer.
The APK contains a stripped copy of `libut3.so`; the original build output retains its symbols for
profiling. The receipt records both `library_sha256` (original) and `packaged_library_sha256` (APK copy).
Removing non-allocated symbol tables saves download and storage space, not resident RAM.

The 0.3-dev APK uses the standard `-O2` build above. Earlier release APKs also used profile-guided
optimization from a profile recorded on a device and `-mtune=cortex-a77`.

Selected UE3 hot paths are implemented as native algorithms in `android/host/game_native.c`,
`render_native.c`, `render_hash_native.c` and `render_draw_native.c`: actor transforms and iteration,
matrix multiplication, sphere/frustum tests, container lookups and static draw-list traversal. Their
adapters preserve the 32-bit guest ABI; the remaining engine and draw policies still use the lifted code.
The build redirects both indirect calls and direct calls, including calls in the same generated source
file. These implementations use C to share the existing host/runtime; changing the language or a file's
extension alone does not remove the guest register model or establish a speedup.

`render_visibility_native.c` implements the connected `00F01CF0` primitive/view and light visibility
traversal, including history setup, restriction filtering, culling decisions and visibility lists.
Local bit operations and sparse advancement avoid guest helper calls; engine policy, occlusion-query
and rendering callbacks retain their ABI and ordering. `render_sparse_native.c` also ports the shared
`005DAF50` allocation-bit iterator initializer used by visibility and draw traversal. Original bodies
remain available for differential testing; absence from one captured match does not make other game
functions safe to remove.
`render_bitarray_native.c` shares a native context across the four bit-array storage operations
(`00BF81A0`, `00BF82A0`, `00BF82F0`, `00BF8340`), preserving inline storage, heap ownership and deep
assignment. `render_light_native.c` implements the `00EFF830` per-view light-array destruction loop
and capacity reset; actual DPG destruction and allocation remain engine callbacks.
`render_occlusion_native.c` implements the `0117AEC0` temporal occlusion decision and its `01178C60`
bounds helper: prior-query results, conservative visibility checks and query scheduling. Internal
bounds preparation stays in the native context; history storage, RHI/query ownership and RNG remain
callbacks. Explicit floating-point operand order preserves NaN payload behavior.

Native AI algorithms in `ai_native.c`, `ai_reach_native.c` and `ai_perception_native.c` cover weighted
navigation graph search, ReachSpec support checks and directional path costs, and perception candidate filtering
and controller traversal. `ai_trace_native.c` implements the segment/AABB test used by collision traces
for perception and movement. Scripted bot behavior, virtual cost/visibility policies, perception events
and recursive octree traversal retain their original implementations. The ports preserve callback order,
guest frames, numerical rounding and cooperative scheduling; they do not reduce the bot population or
the frequency of AI updates.

For differential checks, `userdata/game-native`, `render-native` and `render-hash-native` accept `0`
(lifted), `1` or no file (native), and `2` (compare results, log mismatches, retain the original result).
Checks that would release the guest machine to another thread avoid executing that handover twice.
`render-draw-native` accepts `0`/`2` (lifted) and `1` or no file (native): drawing cannot be run twice
for a live comparison. Its standalone fixture compares the callback sequence, arguments, state and
memory changes with the original traversal instead. These developer diagnostics are cached until the process restarts.
`render-visibility-native`, `render-sparse-native`, `render-bitarray-native`, `render-light-native`
and `render-occlusion-native` are experimental: `1` enables native; `0`, `2` or no file runs the
original once. These nine implementations passed differential checks but were slower in the
November 7 Torlan ABBA comparison. The November 8 visibility revision removes duplicate register
storage and uses the existing separated guest-state compiler profile for this module only. In paired
ARM64 kernel tests it uses 13.79% less CPU than the previous native traversal and 6.53% less than
the lifted original. The November 8 Torlan ABBA comparison did not establish a whole-game gain:
pooled FPS was 28.143 before and 26.857 after, with substantial thermal/workload drift and opposite
pairwise results. The modules remain opt-in. Earlier native modules keep their existing defaults.
Their fixtures compare guest register/flag state, callback/yield boundaries and
memory mutations, including aliasing and floating-point edge cases, against the lifted originals.

The Android settings page also offers **Execution mode**: **Optimal (ARM64)** keeps the existing
native defaults above; **Exact (x86)** forces the recompiled originals for all 30 manual game,
render, AI and PhysX targets, overriding individual diagnostic files. The selection is saved as
`executionMode=0`/`1` in `UTAndroidPort.ini` and applies after a process restart. Both modes run as
ARM64 code and keep the platform and networking compatibility fixes. Exact selects original
algorithms; it is not a complete bit-exact x86 floating-point emulator or a multiplayer guarantee.
See [execution-mode contracts and precision limits](research/execution-modes.md).

**Masked call protocol (November 8).** `tools/call_protocol.py` (the Heroes V whole-program analysis,
ported to UT3's module layout, with capstone's `fcompi`/`fucompi` spelling added to the flag table)
writes `build/call-protocol*/summary.json`: per lifted body the callee-saved registers it may read on
entry or change, and which bodies' callers read flags. `tools/build_android.py --call-protocol SUMMARY`
compiles every lifted body with its registers in C locals and rewrites calls, tails and exits through
`tools/protocol_transform.py`: a call publishes and reloads EAX/ECX/EDX/ESP plus what the callee
summary requires, flags only where a reader exists, and an exit republishes every callee-saved
register the body itself left stale in the globals (clean exits), so host code and full-protocol
callers still find the values of the full protocol. Bodies the host replaces (`--wrap` targets and all
`void_dispatch_add` overrides), import slots of non-ABI exporters (`_setjmp3`) and the analysis'
problem sites keep the full protocol. `--warm-functions LIST` adds per-site indirect-call caches to the
named bodies (`config/warm-functions.txt`: the WAR-Torlan and WAR-Islander censuses); `--protocol-check` builds a validation library that logs every
callee changing a register its summary preserves (`[protocol]` lines; none in a Torlan match or the
menus on 2026-11-08). The three options that shape the lifted code (`--call-protocol`, `--warm-functions`,
`--x87-locals`) are on by default for a plain build (the summary and the x87 plan are made on first use from
the same inputs, about 15 minutes); `--no-call-protocol`, `--no-warm-functions` and `--no-x87-locals` turn them off, and
the investigation builds (`--diagnostics`, `--function-coverage`) keep the full protocol unless they are named.
On the RMX3851 test phone (WAR-Torlan, 32 bots, three alternating pairs, normal GLES frame smoothing) the
protocol build with the November 8 runtime and kernels below used 12.3% fewer game-thread cycles and
20.2% fewer instructions per frame and ran 8.0% more frames per second than the session's starting
build; the APK shrank by 2.2 MB. Method and limits: [research/native-port-2026-11-08.md](research/native-port-2026-11-08.md).

Contract kernels (`core_native.c`, `collision_native.c`, `particle_native.c`, `crt_native.c`) produce the
call contract of the original: EAX, ESP, the preserved EBX/ESI/EDI/EBP and guest memory, and leave ECX,
EDX, the flags and XMM untouched (a caller that relies on a callee preserving them keeps its values;
the original's path-dependent scratch values are not readable by compiled callers). They cover
`004B49A0` TMap<FName>::Find, `00B65BD0` TMap<int>::Find, the `0116C250` segment/box slab test, the
`00BF8DA0` baked distribution lookup used by particles, and msvcr80 `memcpy`/`memmove`/`memset` through
their import slots. `userdata/core-native`, `collision-native` and `particle-native` accept `0`
(lifted), `1` or no file (native) and `2` (run both, log contract differences); Exact mode keeps all of
them on the originals. `tests/test_contract_native.py` compares them with the lifted originals on the
device. The runtime also keeps each COM object's host pointer in its guest half, reads
`QueryPerformanceCounter` from the ARM generic timer and compiles the native kernels with the
separated guest-state profile.

**Game-thread hot spots (November 8, afternoon).** `tools/x87_locals.py` (the Heroes V tool, with the
BioShock port's CRT x87 imports and deeper float-result search) keeps the x87 stack in C locals for the
bodies `tools/x87_slots.py` leaves on the global x87 window: calls with x87 values on the stack, float
results of callees and `fild qword` shadows. `build_android.py --x87-locals PLAN` applies its plan
(`python tools/x87_locals.py`, written next to the call-protocol summary) after the masked protocol;
3913 bodies move off the window, among them `00F01CF0` and the collision octree `01158E40`, and
`libut3.so` shrinks by about 10 MB. Polymorphic indirect-call sites get a four-target extension and
megamorphic ones a 256-entry table of their own (`core.c`; `userdata/site-cache`: `0` = the single-entry
caches, `2` = a `[sites]` statistics line). `query_native.c` polls the D3D9 query of the RHI's wait
`01590CF0` before reading the clock (`userdata/query-wait-native` `0` = original; Exact mode keeps the
original); `QueryPerformanceCounter` reads `CNTVCTSS_EL0` on FEAT_ECV cores; `D3DXMatrixInverse` uses
2x2 minors in Optimal mode (Exact mode and non-finite matrices keep the cofactor form). On the RMX3851
test phone (WAR-Torlan, 32 bots, three alternating pairs each) the runtime changes used 5.0% fewer
game-thread cycles per frame than the morning's protocol build and the x87 locals 3.0% fewer again
(every pair in the same direction); FPS changes stayed within the run-to-run noise. Method and limits:
[research/native-hotspots-2026-11-08.md](research/native-hotspots-2026-11-08.md).

**Native D3D9 RHI (November 8, goal B).** UE3's `FD3D9DynamicRHI` makes one D3D9 COM call per value (a
texture with its sampler state is nine calls; about 41 000 COM calls per frame in a 32-bot WAR-Torlan match). `d3d9_device.c`
defines each state change once (`api_set_*`, `api_draw_*`) for its COM methods and for the native RHI methods of
`rhi_native.inc` (sampler, blend, stencil, depth, rasterizer and color-write states, stream sources, bound shader state,
shader parameters, view parameters, and the draws: `DrawIndexedPrimitive`, `DrawPrimitive` and both UP draws with their
statistics counters). A setter marks a piece of the device state only when its value changes (an unchanged value costs a
compare, the same object already bound costs nothing), `d3d_rt_sync_state` sends the marked pieces, `void_com_host` answers
known objects from a table instead of reading two scattered cache lines, and the render thread is recognised by its thread
pointer. DXVK devices, recording state blocks, traces and the original's error paths keep the COM path; the UP draws keep
their post-effects hook. The native path is the default: `userdata/rhi-native` absent or `1` = native, `0` = the COM path,
`2` = the COM path plus comparisons of the device state and the draws with the native mapping (`[rhi] mismatch` lines, none in
a bot match); Exact mode keeps the COM path. On the RMX3851 test phone (WAR-Torlan, 32 bots, three alternating pairs, frame
smoothing enabled) the native path used 2.2% fewer game-thread cycles and 4.6% fewer instructions per frame than the COM path of the
build before it. The final build of the day (this path, the object table, the native functions and the larger call-cache list
of the next paragraph) against the same older build, four alternating pairs: 4.0% fewer game-thread cycles per frame (median
pair 1.7%; pairs +0.7%, -2.0%, -13.1%, -1.3%) and 11.9% fewer instructions (-7.7% to -18.4% in every pair). The render thread's
CPU time per frame grew by 7.0% (0.8 ms), and the frame rate did not improve (31.7 against 32.0 FPS; pairs from -7.2% to +8.7%,
with the prime core's clock between 2087 and 2670 MHz regardless of the build). A paused scene is pixel-identical between the two paths
except the animated minimap, which differs the same way between two shots of one path (the path can be
switched at run time with `userdata/rhi-flip` for that comparison). The map of the RHI and the next stages:
[research/d3d9-rhi-map-2026-11-08.md](research/d3d9-rhi-map-2026-11-08.md).

**A fight on WAR-Islander (November 8).** A scripted combat scenario (a passive bot in
front of the player, every weapon and fire mode, HUD and first-person weapon on screen) gave a profile and a census: 5 919
functions entered during the fight, 27 291 from process start. The most expensive ones became native:
`TStaticMeshDrawList::DrawVisible` of four further draw lists (`00F05700`, `00F05AA0`, `00EFE060`, `00EFE230`, one template
with `00F058D0`, `render_draw_list.inc`), the UnrealScript VM's instance and local variable opcodes (`00BC1310`, `00BC12C0`) and
three `TMap` finds (`004B9D60`, `00F78830`, `00E2DC90`). All functions of the census got per-site indirect-call caches.
In that fight with a fixed view (three alternating pairs, normal GLES frame smoothing) the final build against the build with only the
native D3D9 work used 0.6% fewer game-thread cycles (pairs -2.0%, -2.8%, +3.3%: within the run-to-run noise) and 2.0% fewer
instructions per frame (pairs -2.3%, -5.4%, +1.7%); the frame rate did not change measurably (43.0 against 43.3 FPS: a frame takes
23.3 ms and the game thread's CPU time is 16.5 ms, so the game thread does not limit that scene). The other functions of the fight stay compiled lifted code with the masked call
protocol, local registers and x87 locals. Method and limits: [research/combat-profile-2026-11-08.md](research/combat-profile-2026-11-08.md).

**Render-thread CPU (November 9).** The final state filter had hidden redundant dirty marks after UP draws: resetting an
already empty stream/index binding still sent up to four 64-byte state pieces, roughly a thousand times per Torlan frame.
The shared UP methods now mark only values they actually clear, including while recording a state block; large float-constant
uploads mark only changed pieces in their existing copy pass. Shader-program lookup mixes aligned pointer bits and reuses the
exact current key, the render queue retires at most 48 records per publication, and attributes sharing a buffer avoid duplicate
`glBindBuffer` calls within a draw. Previous native paths remain enabled. A sixth `DrawVisible` instantiation, `00F05530`, uses
the existing native template and passes the expanded 6 x 3,000-case differential fixture.

Against b6 on the same RMX3851 (GLES, WAR-Torlan, 32 bots, two alternating pairs), r4 used **8.2% fewer render-thread cycles
per frame** (pairs -5.4% and -10.9%; -7.4% per draw) and **6.2% less render CPU time**, 14.79 to 13.88 ms/frame. Queue traffic
fell from 7,105 to 6,064 records/frame and 1,658 to 1,309 KiB/frame (-14.7% and -21.0%). Game-thread cycles fell 3.4%; FPS
27.50 to 27.77 (+1.0%) is within the observed variability. These are measurements of this scene and phone, not a general FPS
claim. Real GLES fixtures cover the UP resets, state-block references and A/B/A attribute buffers, both inline/threaded and
with/without the streaming ring. Method, intermediate runs and limits: [research/render-cpu-2026-11-09.md](research/render-cpu-2026-11-09.md).

**Calibration correction.** The older benchmark scripts requested `bSmoothFrameRate=FALSE` before launch, but
`GameSettings.prepare` wrote `TRUE` afterwards on GLES. The comparisons above used the normal adaptive limiter; the repeated
r4/b6 pair confirms `True`, max 60, from the live engine. Earlier descriptions of these series as "smoothing off" were
incorrect, and their FPS is not unrestricted throughput. Subsequent diagnostic runs use `set Engine.GameEngine
bSmoothFrameRate FALSE` after launch and assert the live `getall` result before recording. Raw reports retain their original
requested setting; the analysis distinguishes it from a verified runtime value.

**Further native work (November 9, c3).** Pure UP draws avoid the second index-bounds scan, vertex elements are resolved
once per draw, and long float-uniform arrays skip equal 64-byte blocks before refining the changed range. Four shader
descriptor bridges and the early sphere rejection of `0117D890` have native C implementations; five explicitly checked
call contracts preserve the original masked register boundary. Raw and real masked-call differential tests cover the new
paths, alongside real GLES buffer/readback tests. On the RMX3851, three alternating Torlan 32-bot pairs gave **3.05% fewer
render instructions per draw** (all three pairs negative). Mean game/render cycles per frame were -2.00%/-1.16%, but two
pairs increased and the first pair dominates: **a stable whole-thread cycle or FPS gain is not demonstrated**. This series
uses live smoothing False verified before and after each run and must stay separate from the earlier normally smoothed
matrix. Full results, failed intermediate candidates, scanner microbenchmarks and limits:
[research/native-cycles-2026-11-09.md](research/native-cycles-2026-11-09.md).

The AI switches are `userdata/ai-native`, `ai-reach-native`, `ai-perception-native` and `ai-trace-native`:
`0` selects the original and `1` or no file selects native. Mode `2` compares the side-effect-free reach
and segment tests; navigation search and perception keep the original because their callbacks can run
scripts or mutate the world. Standalone fixtures compare those callbacks and graph/list mutations.

The Android `benchmark=true` intent starts `WAR-Torlan?NumPlay=32?SpectatorOnly=1?QuickStart=1`
directly: the normal maximum of 32 AI players plus a local spectator. `QuickStart=1` starts the match
without a human player joining a team. Verify the live population and match state before measuring.
Performance comparisons require the same device, resolution, renderer and view, and no other task using
the device. Standalone checks run with an explicit device:

```powershell
python tests/test_game_native.py --serial DEVICE
python tests/test_render_native.py --serial DEVICE --cases 200000
python tests/test_render_hash.py --serial DEVICE
python tests/test_render_draw_native.py --serial DEVICE
python tests/test_render_visibility_native.py --serial DEVICE
python tests/test_render_sparse_native.py --serial DEVICE
python tests/test_render_bitarray_native.py --serial DEVICE
python tests/test_render_light_native.py --serial DEVICE
python tests/test_render_occlusion_native.py --serial DEVICE
python tests/test_ai_native.py --serial DEVICE
python tests/test_ai_reach_native.py --serial DEVICE
python tests/test_ai_perception_native.py --serial DEVICE
python tests/test_ai_trace_native.py --serial DEVICE
python tests/test_contract_native.py --serial DEVICE
python tests/test_site_cache.py --serial DEVICE --objects build/<task>/obj
python -m unittest tests.test_native_same_unit_calls
```

For an exact guest-function entry inventory, build a separate diagnostic library with
`tools/build_android.py --shared --function-coverage --output build/coverage/libut3.so` and package it
normally. The build instruments copies of generated code, native wrappers and explicit guest overrides;
it does not edit canonical generated sources. Write `120000 60000` to
`userdata/function-coverage.request` (milliseconds of warmup and recording). After
`function-coverage.status.json` reports `COMPLETE`, pull `function-coverage.bin` and decode it with:

```powershell
python tools/function_coverage.py decode --manifest build/coverage/function_coverage/manifest.json --binary CAPTURE.bin --output CAPTURE.json
python -m unittest tests.test_function_coverage tests.test_apk_symbols
```

The decoder verifies the manifest identity and emits cumulative and recording-window JSON/TSV lists.
All instrumented threads contribute to exact presence bitmaps; this is not a call count or CPU profile.
Host/libc/Android/driver functions are outside this guest inventory. Instrumentation changes timing and
memory usage, so use an ordinary build for performance and RAM comparisons. Coverage is absent from
ordinary builds.

## Install and supply the game

1. Install the APK (Android 11+, arm64-v8a) and open **Unreal Tournament 3**; the launcher starts.
2. Copy the `Binaries`, `Engine` and `UTGame` folders of the Steam installation to the phone, for example to
   `/sdcard/UT3/game`, or pack them into a ZIP. In the launcher choose **Folder…** or **ZIP archive…**. A ZIP whose
   files are stored without compression (7-Zip: compression level *Store*) can be played straight from the archive.
3. Choose a renderer. On Adreno 6xx/7xx, **DXVK (Turnip)** can use your own Turnip driver: **Own driver…** takes an
   adrenotools driver package or the driver `.so`; the driver check then runs for it, and **Use the port's driver**
   goes back to the bundled one.
4. **Play**. The game's settings, saves and logs are in `Android/data/org.ut3.port/files/userdata`.

The launcher's **Textures** card can create an ASTC or ETC2 cache for game folders or a ZIP played in place.
Stored ZIP entries are read through the same index and descriptor as the game, including archives opened
by Android's document picker; extracted sidecar files take precedence. The cache is
`UTGame/port-textures.tvt`; conversion requires additional storage and preserves the original packages.
It is used by the GLES renderer. DXVK uses the device's native BC texture support. Missing or changed DXT
data is always accepted through the ordinary loader, including partial texture updates made by mods.
The cache stores original mip data so such updates also retain previously uploaded levels and cube faces.

Android allows unused texture mip levels to stream out even at maximum quality (`OnlyStreamInTextures=False`
in the active settings and all five presets). GLES expands unsupported DXT blocks to RGBA8 without this
optional cache, so retaining every visited mip can exhaust shared RAM. The ordinary DXT upload path uses
packed RGBA rows to reduce conversion stalls; the requested texture quality and screen percentage remain
the game's settings.

In the game's **ANDROID** settings, **Control layout** opens the editor: drag a control, pinch or use the
size slider, select hidden controls from the list, and choose **Save** or **Cancel**. The tools panel can be
dragged by its heading. Weapon-bar swiping starts enabled; icon taps start disabled. Bar size is 50–100%,
wheel size 50–150%, both multiplied by the interface scale. The renderer is chosen in the launcher.

For GLES troubleshooting, a `render-thread` file in the userdata directory containing `0` disables the
render thread; `1` enables it. An optional `rt-cores` file selects `auto` (the default), `big`, `next` or
`any`. Environment variables `VOID_RENDER_THREAD` and `VOID_RT_CORES` override these files.

## Checks

```powershell
python -m unittest tests.test_pe_images        # PE images made from the Steam files equal the recompiled ones
python tests/test_game_settings.py
python tests/test_touch_controls.py
python tests/test_native_settings.py          # original UE3 VM and SaveConfig
python -m unittest tests.test_crash_report
python tools/test_openal.py --run --serial DEVICE_SERIAL  # actual EAX and AAudio output
python -m unittest tests.test_openspy tests.test_native_bundle tests.test_ui_hud tests.test_ui_list_scale
python -m unittest tests.test_render_thread_cores tests.test_dxvk_scaled_spirv tests.test_dxvk_pipeline_cache_prune
python -m unittest tests.test_text_dialog tests.test_weapon_gestures tests.test_localized_options tests.test_fsincos
python -m unittest tests.test_dxvk_query_pools tests.test_fps_counter
python tools/check_texture_cache.py --device DEVICE_SERIAL --game /sdcard/UT3/texture-qa
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
