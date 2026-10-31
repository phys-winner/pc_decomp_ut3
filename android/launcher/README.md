# Port launcher base

A template launcher that every static-recompilation port of this family builds into its own
APK: game files from a folder or a ZIP (unpacked, or played straight from a stored ZIP), several
distributions, renderer choice (OpenGL ES, DXVK, Turnip, also with the player's own Turnip driver package) with
capability checks, cleanup of unused files,
texture conversion, GitHub update checks passed on to the game, the port's version and GitHub link.
English, Russian, German and Polish. Target SDK 37.

The base's Java sources live in the template package `port.launcher` and are rewritten into the game's own
package (`<application id>.launcher`) at build time; a game describes itself in `android/port.json`.

| Path | What |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | Decisions, analysis of the mature ports, architecture, status (Russian) |
| [docs/INTEGRATION.md](docs/INTEGRATION.md) | Adding the base to a game; port.json reference; the UT3 example (Russian) |
| [docs/UPDATES.md](docs/UPDATES.md) | Update checks through GitHub releases (Russian) |
| [docs/ICONS.md](docs/ICONS.md) | Icon requirements and checks (Russian) |
| `android/src/port/launcher/` | The launcher (Java 8 language level, no Gradle, no AndroidX) |
| `native/` | `port_env.h` (what the launcher passes to native code), `zipmount.c` (stored-ZIP file layer), `vulkan_probe.c` (the Vulkan driver probe run in `:probe`) |
| `tools/launcher.py` | Renders the sources, expands the manifest markers, writes assets, builds the probe library |
| `tools/icons.py` | Checks adaptive icons (layers, safe zone) |
| `tools/update_manifest.py`, `ci/update-manifest.yml` | Builds `update.json` from releases and attaches it to the latest one |
| `examples/homm5.port.json` | A multi-distribution specification |

```powershell
python tests/run_tests.py                                   # Java selfcheck, zipmount, probe (NDK), tools
python tools/launcher.py check --spec GAME/android/port.json
python tools/icons.py check GAME/android/app/res
python tools/update_manifest.py --repo OWNER/NAME --out update.json --state versions.json
```

Heroes of Might and Magic V (three editions, Android 8+) builds on the base as well; see docs/INTEGRATION.md §8.

Status: the base and the Unreal Tournament 3 integration build and run on the test Pixel 10a (Android 17,
Mali-G715): first start without the game's library in `:launcher`, permissions, folder and ZIP import with the
system picker and the built-in browser, copy and move, the Vulkan probe, languages, Play and back.
