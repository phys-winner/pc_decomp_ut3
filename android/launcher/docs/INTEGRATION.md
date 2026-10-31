# Подключение базы к игре

База — заготовка: при каждой сборке её Java-исходники копируются в **собственный пакет игры**
(`<пакет приложения>.launcher`, например `org.ut3.port.launcher`), а всё, что отличает игры без логики,
описывается в `android/port.json`. Пример полного подключения — Unreal Tournament 3 (§7).

## 1. Подмодуль

```powershell
git submodule add https://github.com/<owner>/<launcher-repo>.git android/launcher
git submodule update --init
```

Сборка игры ищет базу так: переменная `PORT_LAUNCHER` → `android/launcher` (подмодуль) → соседняя папка
`../launcher` (удобно, пока репозиторий базы не опубликован или игра ещё не в git, как ut3 сейчас).

## 2. `android/port.json`

Проверка: `python android/launcher/tools/launcher.py check --spec android/port.json`.

| Поле | Значение |
|---|---|
| `schema` | `1` |
| `app.label` | имя приложения (уведомления, плашки) |
| `app.title`, `app.subtitle` | шапка лаунчера; текст или `{"en": …, "ru": …, "de": …, "pl": …}` |
| `app.javaPackage` | пакет базы; по умолчанию `<package из манифеста>.launcher` |
| `app.logTag` | тег logcat игры (справочно) |
| `app.storage` | папка порта в общей памяти: `{storage}` = `/sdcard/<storage>` |
| `app.github` | `owner/repo`; пусто — без ссылки на GitHub и без проверки обновлений |
| `app.updates` | `false` — не проверять обновления даже при заданном `github` |
| `app.languages` | подмножество `en`, `ru`, `de`, `pl` для выбора языка |
| `app.hooks` | класс игры, реализующий `PortHooks` (необязательно) |
| `app.allFilesAccess` | игра читает файлы по путям вне папок приложения → «Доступ ко всем файлам» (по умолчанию `true`) |
| `app.localNetwork` | LAN-игра → разрешение `ACCESS_LOCAL_NETWORK` (Android 17), спрашивается перед первым запуском |
| `app.autoLaunch` | значок сразу запускает последнюю игру, если её прошлый старт этой сборки дошёл до первого кадра |
| `app.processes.launcher` | процесс лаунчера (по умолчанию `:launcher`; игра остаётся в своём) |
| `app.licenses` | файлы ассетов и папки (с `/` на конце) для экрана «Лицензии»: строка или `{"path": …, "title": …}` (понятное имя вместо имени файла); `licenses/` добавляется всегда |
| `app.mountSidecarLimitMB` | сколько сжатых файлов архива допустимо распаковать рядом при игре из архива (256) |
| `theme.*` | `background` (массив цветов градиента), `card`, `cardStroke`, `cell`, `accent`, `accentDark`, `primaryFill`, `text`, `muted`, `error`, `good`, `titleFont` (`sans-serif-medium`, `serif-bold`, …) |
| `distributions[]` | дистрибутивы игры (см. ниже) |
| `renderers` | `default`, `modes[]`: `id`, `kind` (`gles`/`vulkan`/`turnip`), `title`, `description`, `vulkan` (`"1.3"`), `minSdk`, `adreno` (`[600, 799]`), `libraries` (что должно лежать в `nativeLibraryDir`), `driver` (для `turnip` обязательно: библиотека драйвера, которую проба грузит через adrenotools), `needsBC` (игре нужны BC1–3; без них режим недоступен) |
| `options[]` | `key`, `type` (`bool`/`choice`/`text`), `default`, `title`, `hint`, `only` (id дистрибутивов), `choices` (`value`, `title`) |
| `cleanup.categories[]` | `id`, `part`, `title`, `hint`, `globs` (`*`, `?`, `**`; `папка/` — папка целиком; без учёта регистра) |
| `textures.backend` | класс игры, реализующий `TextureBackend` (конвертация DXT → ETC2/ASTC) |
| `hints.gameFiles` | подсказка в карточке «Файлы игры» |
| `hints.options` | подсказка внизу карточки опций (например, где настройки самой игры) |

Дистрибутив:

| Поле | Значение |
|---|---|
| `id` | `[a-z0-9_]+` |
| `title`, `version` | для карточки |
| `activity` | полное имя Activity игры этого дистрибутива |
| `renderers` | какие режимы из `renderers.modes` ему доступны |
| `intro` | `{"default": true}` — показать галку «Пропускать вступительные ролики» |
| `parts[]` | части файлов: `id`, `title`, `folder` (шаблон: `{storage}`, `{files}`, `{private}`), `fixed` (порт читает часть только из этой папки — импорт копирует/переносит/распаковывает туда), `marker` (путь, по которому часть узнаётся в папке или архиве), `required` (что должно быть; папки — с `/` на конце; регистр не важен), `needed` (что брать при импорте; по умолчанию всё), `mount` (разрешить игру прямо из несжатого ZIP) |

Части нескольких дистрибутивов в одной папке архива (Heroes V: Hammers of Fate ставится поверх исходной
игры) распаковываются один раз — с тем, что нужно каждому, — в папку первого из них, и все они берут эту
папку; цели одного импорта никогда не совпадают (вторая получает `имя-2`). Папка, выбранная кнопкой
«Папка…», тоже назначается каждому найденному в ней дистрибутиву.

## 3. Манифест

- `targetSdkVersion 37`; на `<application>` — `android:icon`/`android:roundIcon` и, для игр,
  `android:appCategory="game"` (иначе на планшетах Android 16+ игнорирует ориентацию).
- Две метки — их раскрывает `tools/launcher.py`:
  - `<!-- launcher:manifest -->` внутри `<manifest>`: разрешения (All files access, уведомления,
    foreground-служба dataSync, wake lock, `ACCESS_LOCAL_NETWORK`, `INTERNET` при обновлениях) и `<queries>`
    (пикеры, браузер, пакеты DocumentsUI — без них на targetSdk ≥ 30 их не видно);
  - `<!-- launcher:application -->` внутри `<application>`: `LauncherActivity` (MAIN/LAUNCHER),
    `FileBrowserActivity`, `Work$WorkService`, а при Vulkan-режимах — служба пробы `Probe`
    (`android:process=":probe"`).
- У Activity игры **убрать** `MAIN/LAUNCHER` (точка входа — лаунчер); `exported="true"` можно оставить
  для `am start` из инструментов. Уже объявленные игрой разрешения не дублируются. Итог проверяется как XML.
- `minSdkVersion` ниже 30 (Android 8–10, где нет «Доступа ко всем файлам»): база добавляет
  `READ/WRITE_EXTERNAL_STORAGE` с `android:maxSdkVersion="29"` (их лаунчер и спрашивает; после отказа
  «больше не спрашивать» кнопка открывает страницу приложения в настройках) и, если его нет,
  `android:requestLegacyExternalStorage="true"` на `<application>`: иначе Android 10 при targetSdk ≥ 29 не
  даст читать файлы игры по путям.
- В XML-комментариях нельзя `--` (например, `--es`): aapt2 откажется.

## 4. Сборка

```python
sys.path.insert(0, str(LAUNCHER / "tools"))
import launcher as port_launcher
prepared = port_launcher.prepare(ROOT / "android/port.json", ROOT / "android/app/AndroidManifest.xml", stage / "launcher")
shutil.copytree(Path(prepared["assets"]) / "launcher", assets / "launcher")          # port.json, build.json
run(aapt2, "compile", "--dir", ROOT / "android/app/res", "-o", stage / "resources.zip")  # значки
run(aapt2, "link", "-I", android_jar_37, "--manifest", prepared["manifest"], "-A", assets, stage / "resources.zip", "-o", base_apk)
sources = game_sources + [Path(p) for p in prepared["sources"]]                    # javac --release 8
if prepared["probe"]:                                                                # есть Vulkan-режимы
    probe = port_launcher.build_probe(stage / "probe", prepared["package"])          # → lib/arm64-v8a/ в APK
```

`build_probe(папка, пакет, ndk=None)` компилирует `native/vulkan_probe.c` clang'ом NDK (API 30,
`-Wl,-z,max-page-size=16384` для страниц 16 КБ) и возвращает путь к `libportprobe.so`; NDK ищется через
`ANDROID_NDK_HOME`/`ANDROID_NDK_ROOT`/SDK (`ndk_root()`). То же из консоли:
`python tools/launcher.py probe --package <пакет> --out <папка>`. Библиотека регистрирует JNI на
`<пакет>.Probe` и грузится только в `:probe`.

`build.json` (versionName, versionCode, коммит, время) лаунчер показывает в шапке и в «О порте».
Значки проверяются `python android/launcher/tools/icons.py check android/app/res` (см. ICONS.md).

## 5. Код игры

```java
import <пакет>.launcher.Launch;

launch = Launch.attach(this);              // onCreate, до native-кода: PORT_* в окружении процесса
launch.renderer();                          // выбранный лаунчером рендерер (также extra "renderer")
launch.language();                          // язык интерфейса лаунчера (пусто — системный)
launch.starting();                          // игра действительно стартует (снимает прошлый отказ)
launch.failed(reason);                      // игра отказалась стартовать: лаунчер покажет reason, автозапуск выключен
launch.firstFrame(rootFrameLayout);         // игра показала первый кадр: запуск засчитан, плашка обновления
launch.exited(code);                        // игра завершилась: 0 — назад в лаунчер (процесс игры закрывается)
launch.exiting(0, next);                    // то же, когда native-код сам вызывает exit() сразу после JNI-обратного
                                            // вызова: процесс не убивается (Android запишет код выхода игры);
                                            // next — дистрибутив, который лаунчер сразу запустит, или null
Launch.openLauncher(this, launch.distribution());   // пункт «Лаунчер» в меню игры
Launch.openLauncher(this, "hof", true);     // «Сменить игру»: лаунчер запускает этот дистрибутив, как кнопка
                                            // «Играть», а если он не готов — показывает, чего не хватает
Launch.ready(context);                      // id дистрибутивов, чьи файлы на месте (список для «Сменить игру»)
```

Лаунчер считает игру идущей («Вернуться в игру», автозапуск возвращает в неё без новой записи запуска),
только пока жив её процесс: задача, оставшаяся в недавних после exit() или падения, — уже не игра.

- **targetSdk 36+**: на Android 16+ система не вызывает `onBackPressed()` и не присылает `KEYCODE_BACK`;
  регистрируйте `getOnBackInvokedDispatcher().registerOnBackInvokedCallback(...)` (API 33+) и оставьте старый
  путь для устройств, где предсказуемый «назад» выключен.
- `PortHooks` (класс из `app.hooks`): `renderer()`/`setRenderer()` — если выбор рендерера хранится в
  настройках самой игры (UT3: вкладка ANDROID), `prepare()` — свои extras для запуска, `missing()` — чего ещё
  не хватает части сверх `required` (UT3: файлы выбранного в игре языка), `migrate()` — один раз на процесс,
  до чтения выбора игрока: у игры был свой лаунчер — его настройки переносятся через
  `Launch.adopt(context, map)` (ключи базы: `part.<дистрибутив>.<часть>`, `option.<key>`,
  `intro.<дистрибутив>`, `renderer.<дистрибутив>`, `last`; берётся только то, чего игрок здесь ещё не выбрал).
- `TextureBackend` (класс из `textures.backend`): `scan`, `convert`, `undo`, `cancel`, `progress`, `message`
  поверх контейнера игры; карточка, фоновая работа и прогресс — в базе. Необязательно: `removeOriginals()` —
  удалить оригиналы после конвертации ради места (`Scan.removable` — сколько освободится, кнопка с
  подтверждением; после этого `Scan.originalsRemoved` прячет «Вернуть оригиналы»).

## 6. Native-код

- `android/launcher/native/port_env.h`: `PORT_DISTRIBUTION`, `PORT_VERSION_NAME/CODE`, `PORT_LANGUAGE`,
  `PORT_RENDERER`, `PORT_SKIP_INTRO`, `PORT_PART_<ID>`, `PORT_OPTION_<KEY>`, `PORT_TURNIP_DRIVER` (свой драйвер
  Turnip игрока, полный путь; пусто — встроенный), `PORT_UPDATE_VERSION/PAGE`,
  а для частей из архива — `PORT_MOUNT_<ID>` (индекс) и `PORT_MOUNT_FD_<ID>` (открытый дескриптор архива).
  JNI не нужен, имя Java-пакета роли не играет.
- `android/launcher/native/zipmount.c/.h`: слой «только чтение» над несжатым ZIP — `zm_from_env("game")`,
  `zm_find` (без учёта регистра, `\` и `/`), `zm_pread`, `zm_map`, `zm_list`, `zm_sidecar_path`. Подключается
  в файловом слое хоста (`files.c`) перед обычными путями.

## 7. Пример: Unreal Tournament 3 (`pc_decomp_ut3`)

Что изменено (копии исходных файлов — `ut3/build/launcher-backup-2026-10-30/`):

| Файл | Изменение |
|---|---|
| `android/port.json` | новый: дистрибутив `ut3`, части `game` (`/sdcard/UT3/game`; `required` — `CookedPC`, `Localization` и базовые/умолчательные INI) и `images` (`/sdcard/UT3/images`, 20 PE-образов) — обе `fixed`, как их читает native-код; рендереры `gles`/`dxvk`/`dxvk-turnip` (`driver` — `libut3_turnip.so`); уборка установщиков (`directx9c`, `PhysX`, `VCRedist`, `OpenAL`); `localNetwork`; лицензии с понятными названиями |
| `android/app/res/` | новый значок: шестигранная «арена» и прицел (оригинальный, не логотип Epic), monochrome, `ic_notify` |
| `android/app/AndroidManifest.xml` | targetSdk 35 → 37, значок, метки лаунчера, у `GameActivity` убран `MAIN/LAUNCHER`, убран устаревший `requestLegacyExternalStorage` |
| `android/app/src/org/ut3/port/LauncherHooks.java` | новый: рендерер читается/пишется в `UTAndroidPort.ini` — лаунчер и вкладка ANDROID в игре всегда согласованы; `missing()` — файлы выбранного языка (`Localization/<LANG>/`, `Startup_<LANG>.upk`) |
| `android/app/src/org/ut3/port/GameActivity.java` | `Launch.attach` первым делом, `starting`/`failed` во всех ветках старта (тексты en/ru/de/pl), `firstFrame` при первом кадре, `exited` при остановке (0 → лаунчер), «назад» через `OnBackInvokedCallback`, «назад» до старта игры → лаунчер |
| `tools/build_apk.py` | платформа android-37.0, база (`android/launcher` или `../launcher`), значки, `libportprobe.so`, названия лицензий, отметка о лаунчере в квитанции (`launcher.probe_sha256`) |

Сборка (native не пересобирается, библиотека — та же, что в проверенной 0.7.0-touch):

```powershell
python tools/build_apk.py --library build/pgo-a77/libut3.so --output build/launcher-apk/ut3.apk
```

До 0.8.1-dev `android_main.c` жёстко задавал `/sdcard/UT3/game`, `/sdcard/UT3/images` и `-nosplash -nomovie`,
поэтому части были объявлены `fixed`, а галки роликов не было. С 0.8.1-dev (пересобранный `libut3.so`) хост читает
`PORT_PART_GAME` и `PORT_PART_IMAGES` (абсолютный путь; без переменной — прежние папки) и `PORT_SKIP_INTRO`
(без переменной — пропуск, `-nostartupmovies`; `0`/`false` — вступительные ролики; старт бенчмарка оставляет
`-nomovie`). В `port.json` части больше не `fixed`, у дистрибутива `intro.default: true`, `app.github` —
`phys-winner/pc_decomp_ut3`. Для самих роликов Bink хосту понадобились `GetTopWindow`/`GetWindow` в `user32.c`
и обход потока рендеринга под `-onethread` в `loading.c`.

С 0.8.2-dev UT3 играет прямо из несжатого ZIP: у части `game` в `port.json` стоит `"mount": true`;
`android/host/files.c` читает архив поверх папки-sidecar (сначала папка, потом индекс; запись файла, который
есть только в архиве, сначала копирует его в папку), `android/host/zipmount.c` — копия `native/zipmount.c`
с заголовком внутри (новый `.h` под `android/` пересобрал бы все объекты хоста UT3); Java-сторона UT3
(`GameFiles.java`) читает тот же индекс для своих INI и проверки языка. Проверено на Pixel 10: архив 9,9 ГБ,
главное меню, WAR-Torlan, бой.

С 0.1-dev (первая публичная нумерация, versionCode 1) части `images` в `port.json` больше нет: у дистрибутива одна
часть — `game`, и её `required` перечисляет `Binaries/UT3.exe` и 17 DLL. `android/host/pe_images.c` делает PE-образы
при загрузке из `Binaries` данных игры (папка или несжатый ZIP через `files.c`), перебазируя их как
`tools/pe_rebase.py`, и сверяет размер и CRC-32 каждого с `generated/input`; CRT VC80 (`msvcr80.dll`, `msvcp80.dll`
из WinSxS), которого в данных игры нет, вшит в `libut3.so` (`pe_builtin.c`). Свой драйвер Turnip игрока база передаёт
как `PORT_TURNIP_DRIVER`, а `android/dxvk/bridge.cpp` отдаёт его adrenotools вместо `libut3_turnip.so`.

### Свой драйвер Turnip

У режимов `kind: turnip` на Adreno карточка рендерера показывает драйвер и кнопки «Свой драйвер…» / «Вернуть
встроенный» (`TurnipDriver.java`). Принимается пакет adrenotools (ZIP с `meta.json`, где `libraryName` — библиотека;
без `meta.json` — единственная `.so` архива) или сама библиотека AArch64 (ELF64, `ET_DYN`, `EM_AARCH64`). Файл
копируется в `files/launcher/turnip/`, его имя и отпечаток (имя, размер, время) входят в ключ отчёта `Probe`, поэтому
проверка драйвера проходит заново уже для него (`vulkan_probe.c` грузит драйвер, заданный полным путём, из его папки
с хуками порта). В процесс игры `Launch.attach` кладёт `PORT_TURNIP_DRIVER` — полный путь к библиотеке (пусто для
встроенного), и native-код игры передаёт папку и имя в `adrenotools_open_libvulkan`.

## 8. Пример: Heroes of Might and Magic V (порт `homm5`)

Три дистрибутива в одном APK: `toe` (Tribes of the East 3.1, `org.homm5.port.GameActivity`, основной
процесс), `hof` (Hammers of Fate 2.1, `.HofGameActivity`, `:hof`) и `vanilla` (1.6, `.VanillaGameActivity`,
`:vanilla`), по одной части `game` на дистрибутив: `required` — ровно то, что проверяет `Games.installed`,
папки по умолчанию — `{storage}/game`, `game-hof`, `game-vanilla` (как у `tools/deploy.py`), `needed` —
папки, которые читает игра. Только рендерер `gles` (DXVK/Turnip выключены: ни пробы, ни библиотек), опции
`new_creature_framework` (только `toe`) и `lan_peers`, текстуры — `org.homm5.port.TexconvBackend` поверх
`libtexconv.so` (с удалением оригиналов), `autoLaunch`, `localNetwork`, minSdk 26.

| Что | Как |
|---|---|
| `LauncherHooks.migrate` | папки изданий, выбранное издание, ролики, NCF и LAN-адреса из SharedPreferences `launcher` старого лаунчера → `Launch.adopt`; userdata в `/sdcard/HeroesV` не трогается |
| `GameActivity` | `Launch.attach` первым делом; папка — `launch.folder("game")` (или `--es game` инструментов) → прежний `nativeStart`; `failed` при любом отказе; `starting` перед движком; `firstFrame` там, где был маркер старта; `exiting(0, next)` из `onGameExit` (движок сам вызывает `_exit`, SessionLog видит его код); «Сменить игру» — `Launch.ready` + `openLauncher(…, true)`; «назад» — `OnBackInvokedCallback` |
| `tools/build_game_apk.py` | `launcher.prepare`, android-37.0, `assets/launcher`, d8 через файл аргументов (список классов длиннее 8191 символа cmd.exe), ключ `build/android/debug.keystore` не создаётся заново |
