# Проверка обновлений через GitHub

Лаунчер (и игра) узнают о новой версии порта без своего сервера. Единственный источник правды —
сам APK, приложенный к GitHub-релизу: `update.json` собирается из него автоматически.

## 1. Решение

- Релизы публикуются **без флага pre-release**. Тогда GitHub считает самый свежий из них «latest»,
  и постоянная ссылка `https://github.com/<owner>/<repo>/releases/latest/download/update.json`
  всегда ведёт к манифесту последней версии (302 на `release-assets.githubusercontent.com`,
  без лимитов API).
- Сейчас (30.10.2026) все релизы `pc_decomp_homm5`, `pc_decomp_void`, `pc_decomp_strhld` — pre-release,
  поэтому `releases/latest` отвечает 404. Снимите флаг с последнего релиза (Edit → «Set as the latest
  release»), и всё заработает.
- Pre-release'ы генератор пропускает: «тестовые» сборки можно выкладывать с флагом — лаунчеры игроков их
  не предложат.

```
релиз с APK (как сейчас, вручную или через API)
        │  release: published / edited / released / deleted …  + раз в 6 ч по расписанию
        ▼
GitHub Action  (ci/update-manifest.yml → .github/workflows/ игры)
        │  tools/update_manifest.py: релизы → APK → aapt2 dump badging + apksigner
        ▼
update.json + versions.json  →  приложены к релизу, который GitHub считает latest
        │
        ▼
https://github.com/<owner>/<repo>/releases/latest/download/update.json   ← читает лаунчер
```

Почему по расписанию тоже: если APK приложить к релизу уже после публикации, GitHub не присылает
событие — плановый запуск догонит. Пустых загрузок нет: Action сравнивает новый `update.json` с уже
приложенным и пропускает загрузку, если байты совпали.

## 2. Формат `update.json` (schema 1)

```json
{
  "schema": 1,
  "repository": "phys-winner/pc_decomp_void",
  "package": "org.thevoid.port",
  "latest": {
    "versionCode": 5,
    "versionName": "0.3.1-dev",
    "tag": "v0.3.1-dev",
    "title": "v0.3.1-dev",
    "published": "2026-10-29T20:22:36Z",
    "page": "https://github.com/phys-winner/pc_decomp_void/releases/tag/v0.3.1-dev",
    "apk": {
      "name": "the-void-0.3.1-dev-2d965db7.apk",
      "url": "https://github.com/phys-winner/pc_decomp_void/releases/download/v0.3.1-dev/the-void-0.3.1-dev-2d965db7.apk",
      "size": 175062152,
      "sha256": "2d965db77d127a6903370973c5333bce34b9f86c2d7a8397c2f1438303fd378c",
      "signer": "4a5064e655311f55b2c1e55090032f0609d4eb62664225ddeefe47d95dc67579",
      "minSdk": 26,
      "abis": ["arm64-v8a"]
    },
    "notes": "текст релиза (Markdown, до 4000 символов)"
  },
  "history": [
    {"versionCode": 5, "versionName": "0.3.1-dev", "tag": "v0.3.1-dev", "published": "…", "page": "…", "notes": "…"}
  ]
}
```

- `latest` — наибольший versionCode среди релизов без pre-release (`null`, пока таких нет).
- `signer` — SHA-256 сертификата подписи: лаунчер заранее предупреждает, если релиз подписан другим ключом
  и поверх установленной версии не встанет.
- `history` — до 30 версий, у первых 10 есть `notes` («что нового» с вашей версии).
- Новые поля добавляются только обратно совместимо; несовместимое изменение — `schema: 2`.

Проверено на `phys-winner/pc_decomp_void` (с `--include-prereleases`, т.к. там пока всё pre-release):
SHA-256 четырёх APK из `pub/` совпали с digest GitHub, versionCode 5/4/3/2, сертификат прочитан из APK;
без флага `latest` = `null`, как и должно быть.

## 3. Подключение к репозиторию игры (один раз)

1. База — подмодулем `android/launcher` (см. INTEGRATION.md). Скопируйте `android/launcher/ci/update-manifest.yml`
   в `.github/workflows/update-manifest.yml`: workflow сам найдёт `tools/update_manifest.py` в подмодуле.
   (В homm5 `.gitignore` — allowlist: разрешите `.github/`.)
2. Settings → Actions → General → Workflow permissions: **Read and write** (нужно `contents: write`
   для загрузки ассетов).
3. Первый запуск вручную: Actions → Update manifest → Run workflow. Он скачает APK всех релизов один раз
   (кэш `versions.json` дальше путешествует вместе с `update.json`).
4. В `port.json` игры: `"app.github": "owner/repo"` — лаунчер покажет ссылку на GitHub и начнёт
   проверять обновления.

## 4. versionCode

Обновление ставится поверх только с большим versionCode. Сейчас коды ставятся вручную и разные:
homm5 = 6 (0.3.1-dev), the_void = 5, stronghold = 27, ut3 = 10. Рекомендация — выводить из тега:

```
versionCode = MAJOR*1000000 + MINOR*10000 + PATCH*100 + S
S: dev = 10+N, alpha = 30+N, beta = 50+N, rc = 70+N, финал = 99      (N = 0..9)
0.3.1-dev → 30110 · 0.3.1 → 30199 · 0.7.1 → 70199 · 1.0.0 → 1000099
```

Все значения больше нынешних. Теги вида `v0.22-dev` (stronghold) не использовать: по формуле 0.22 > 0.3.

## 5. Клиент (`Updates.java`)

- **Когда:** при открытии лаунчера не чаще раза в 6 ч (переключатель «Проверять обновления
  автоматически»), кнопка «Проверить обновления» — всегда.
- **Запрос:** GET `…/releases/latest/download/update.json` (таймаут 15 с, ответ ≤ 1 МБ); если не вышло —
  `api.github.com/repos/<repo>/releases/latest` → ассет `update.json` (лимит API 60 запросов/ч на адрес).
- **Проверки:** `schema == 1`, `package` = пакет приложения, `versionCode` больше установленного,
  `minSdk` ≤ версии Android, версия не пропущена игроком.
- **Подпись:** `signer` ≠ сертификату установленного APK → красное предупреждение вместо тихой ошибки установки.
- **Состояние:** `files/launcher/update.properties` (атомарная запись под файловой блокировкой) — его
  читает и процесс игры.
- **Интерфейс:** карточка «Доступна версия X (установлена Y)», «Скачать · размер» (браузер скачивает APK
  по прямой ссылке), «Что нового», «Пропустить эту версию»; в «О порте» — ссылки GitHub и «Релизы».

Установка прямо из лаунчера (`PackageInstaller`, `REQUEST_INSTALL_PACKAGES`) не реализована: сначала
стоит проверить на устройстве нынешний путь через браузер.

## 6. Передача в игру

- Перед запуском native-кода `Launch.attach()` кладёт в окружение процесса игры `PORT_UPDATE_VERSION`
  и `PORT_UPDATE_PAGE` (см. `native/port_env.h`) — хост может показать уведомление средствами игры.
- Без поддержки в хосте игра получает уведомление и так: `Launch.firstFrame()` показывает плашку
  «Доступно обновление порта X — подробности в лаунчере» поверх первого кадра, один раз на версию
  (касание закрывает, через 7 с исчезает сама).

## 7. Безопасность и приватность

- Только HTTPS к github.com; в запросах нет идентификаторов устройства (User-Agent: `<package>/<versionName>`).
- Игрок скачивает APK по ссылке, которую видит; SHA-256 и сертификат опубликованы в `update.json`.
- В `update.json` только то, что и так публично в релизе.
