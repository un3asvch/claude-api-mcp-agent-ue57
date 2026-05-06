# Claude Agent — UE5.7 Plugin

> 🇷🇺 [Русская версия](#claude-agent--плагин-для-unreal-engine-57) ниже / [Russian version](#claude-agent--плагин-для-unreal-engine-57) below
>
> 🇬🇧 [English version](#claude-agent--unreal-engine-57-plugin)

---

## Setting the agent's working language / Язык взаимодействия с агентом

The plugin is bilingual by design. The default System Prompt tells
Claude to respond in **Russian** because that's the primary author's
preference. Change it any time:

**Project Settings → Plugins → Claude Agent → Agent → System Prompt**

Edit the prompt directly. To switch to English, replace the line about
language with something like:

```
- Default to English for explanations and inline comments. Keep code,
  type names, and asset paths verbatim as they appear in the project.
```

You can pick any other language ("Default to French...", "Default to
Japanese...") or leave it open ("Match the user's language in their
messages.") — Claude will follow.

The system prompt is the canonical place to control tone, depth,
language, and project-specific preferences. The default already
contains GASP / UE 5.7 context and Russian preference — adjust to
your project.

Плагин двуязычный изначально. Стандартный System Prompt просит
Claude отвечать **по-русски** — это предпочтение основного автора.
Меняется в любой момент:

**Project Settings → Plugins → Claude Agent → Agent → System Prompt**

Отредактируй промпт напрямую. Чтобы переключиться на английский,
замени строку про язык на что-то вроде:

```
- Default to English for explanations and inline comments. Keep code,
  type names, and asset paths verbatim as they appear in the project.
```

Можно ставить любой язык ("Default to French...", "Default to
Japanese...") или оставить открытым ("Match the user's language in
their messages.") — Claude послушается.

System prompt — каноничное место чтобы управлять тоном, глубиной,
языком и проектными предпочтениями.

---

# Claude Agent — Плагин для Unreal Engine 5.7

Полноценный AI-ассистент Anthropic Claude, встроенный прямо в редактор
UE5 как докуемая панель. Работает в режиме агента: сам выбирает, какие
из ~200 инструментов вызвать, читает Asset Registry, разбирает
Blueprint-графы, проверяет логи, правит свойства (с твоим
подтверждением) и компилирует BP.

## Возможности

- **Два backend'а**: Anthropic API (pay-per-token) и Claude Code CLI
  (через Pro/Max подписку — без API-ключа).
- **MCP server** — плагин поднимает локальный MCP-сервер, через
  который Claude Code CLI получает доступ ко всем UE-тулам.
- **Докуемая панель** в редакторе (Tools → Claude Agent).
- **Стриминг ответов** через SSE.
- **Tool use loop** — агентный цикл, в котором Claude сам выбирает
  инструменты.
- **Drag-and-drop ассетов** в окно чата прямо из Content Browser.
- **Контекстное меню Content Browser**: ПКМ по ассету →
  «Ask Claude about this asset».
- **Подтверждение записи**: любой пишущий инструмент спрашивает
  подтверждение через диалог.
- **Project-aware system prompt** — в дефолте уже зашит контекст про
  GASP, UE 5.7, ветки CMC/Mover и Blueprint-first подход.
- **Tool filtering** — 22 категории тулов с галочками вкл/выкл.
  Отключай те системы которые не используешь чтобы Claude быстрее
  находил нужный инструмент и тратил меньше токенов.
- **Sticky conversations** — атомарное сохранение чатов на каждое
  событие, fsync на диск. История переживает крашы редактора.
- **Visual capture** — скриншоты вьюпорта/превью ассетов прямо в
  ответ Claude'а, чтобы он мог реально *видеть* результат.

## Поддерживаемые системы (~200 тулов)

| Категория | Кол-во | Что делает |
|-----------|--------|-----------|
| **Blueprint** | 53 | Создание/редактирование BP, графовые ноды, K2 nodes, navigation |
| **Animation** | 13 | AnimSequence, Montage, AnimBlueprint |
| **Level / Actor** | 13 | Спавн, transform, level design batches (grid/line/duplicate/align) |
| **PIE** | 12 | Runtime debugging в Play mode (актёры, blackboard, console) |
| **PCG** | 12 | Procedural Content Generation (graphs, nodes, volumes) |
| **StateTree** | 12 | Создание и редактирование StateTree |
| **Behavior Tree** | 12 | BT + Blackboard, decorators, services |
| **Control Rig** | 9 | Read-only Control Rig + IK Retargeter editing |
| **Material** | 8 | Material Instance Constants, parameters |
| **GAS** | 8 | Gameplay Ability System (Abilities, Effects, AttributeSets) |
| **Debug** | 8 | Compile errors, broken refs, orphans, dependency tree |
| **Data Asset** | 7 | DA + DataTable с nested property paths |
| **Spline** | 6 | Создание/редактирование сплайнов |
| **Sequencer** | 6 | Level Sequences, bindings, tracks, keyframes |
| **General** | 6 | Project info, asset search, log reading |
| **Pose Search** | 4 | Schema, Database, Motion Matching |
| **Niagara** | 4 | VFX systems, emitters |
| **Environment** | 4 | Lights, fog, post-process, sky atmosphere |
| **Visual** | 3 | Screenshot viewport, capture from camera, asset thumbnail |

Tool filtering позволяет включать/выключать каждую категорию отдельно
в настройках. Например, если ты на GASP — можешь выключить GAS,
Pose Search редко нужен — выключаешь и его.

---

## Установка

### 1. Скопируй плагин в проект

Помести папку `ClaudeAgent` в `<YourProject>/Plugins/`. Должно получиться:

```
YourProject/
└── Plugins/
    └── ClaudeAgent/
        ├── ClaudeAgent.uplugin
        ├── Resources/
        └── Source/
            └── ClaudeAgentEditor/
                ├── ClaudeAgentEditor.Build.cs
                ├── Public/
                └── Private/
```

Если папки `Plugins` ещё нет — создай.

### 2. Перегенерируй project files

ПКМ по `<YourProject>.uproject` → **Generate Visual Studio project files**.

### 3. Собери

Открой `.sln`, выбери конфигурацию **Development Editor** + **Win64**,
собери проект (`Ctrl+Shift+B`).

CLI альтернатива:

```bash
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ^
    YourProjectEditor Win64 Development ^
    -Project="C:\Path\To\YourProject.uproject" -WaitMutex
```

### 4. Запусти редактор

При первом запуске UE спросит, собрать ли отсутствующие модули —
соглашайся. Если плагин не активен по умолчанию: **Edit → Plugins →
найди «Claude Agent» → галочка → перезапуск**.

### 5a. Использование с Anthropic API (платный per-token)

1. Получи ключ на https://console.anthropic.com/ (раздел API Keys).
2. **Edit → Project Settings → Plugins → Claude Agent**.
3. **API Key** → вставь ключ.
4. **Default Model** → `claude-opus-4-7` (рекомендуется),
   `claude-sonnet-4-6` (быстрее/дешевле) или `claude-haiku-4-5`.

Ключ хранится в `Saved/Config/.../EditorPerProjectUserSettings.ini`
(не попадает в Git если `Saved/` в `.gitignore`).

### 5b. Использование с Claude Pro/Max через CLI (бесплатно поверх подписки)

Если у тебя Claude Pro/Max — можно работать через CLI без отдельного
API-ключа.

1. Установи Node.js 18+ (https://nodejs.org/).
2. В терминале:
   ```
   npm install -g @anthropic-ai/claude-code
   ```
3. Запусти `claude` один раз вручную → команда `/login` → авторизация
   в браузере.
4. **Project Settings → Plugins → Claude Agent**:
   - **Backend** → `Claude Code CLI`
   - **Claude Code Binary Path** → пусто (если `claude.cmd` в PATH)
     или полный путь, например
     `C:\Users\You\AppData\Roaming\npm\claude.cmd`.
5. В панели нажми **↻ Reload** для переключения backend'а.

### 5c. MCP server: подключить UE-тулы к CLI (рекомендуется для CLI)

**Лучшая конфигурация**: подписка Pro/Max через CLI + полный набор
UE-тулов плагина. Без MCP server'а CLI видит только файлы проекта на
диске.

Плагин при старте редактора автоматически:
1. Поднимает HTTP MCP server на `127.0.0.1:17812/mcp`.
2. Регистрирует его в Claude Code CLI через `claude mcp add`.

Проверь в Output Log две строки:
```
LogClaudeAgent: MCP server listening on http://127.0.0.1:17812/mcp
LogClaudeAgent: MCP auto-registered with Claude Code as 'unreal' (user scope)
```

Если автоматическая регистрация не сработала — нажми кнопку **MCP**
в toolbar панели чата, или вручную:
```
claude mcp add unreal --transport http http://127.0.0.1:17812/mcp --scope user
```

Проверка: `claude mcp list` → должно быть `unreal: ✓ Connected`.

**Важно:**
- MCP server работает только пока редактор открыт.
- Тулы **не** требуют подтверждения через MCP (особенность CLI-протокола).
- Порт только на localhost — извне недоступен.

### 6. Открой панель

**Tools → Claude Agent**, или Window menu. Док-таб можно перетаскивать
куда угодно.

---

## Использование

### Базовый чат

Введи вопрос в нижнее поле, **Ctrl+Enter** — отправить.

### Прикрепление ассетов

1. **Drag-and-drop** ассета(ов) из Content Browser в окно чата.
2. **Контекстное меню**: ПКМ по ассету → *Ask Claude about this asset*.

### Tool filtering

**Project Settings → Plugins → Claude Agent → Tool Filtering** — 22
галочки включения/выключения категорий. Снимаешь галку → тулы этой
категории не передаются в API.

Полезные пресеты:
- **Чисто понимание проекта**: General + Blueprint + Animation + Visual
- **Работа над AnimBP**: General + Blueprint + Animation + ControlRig
- **Level design**: General + Level + Spline + PCG + Environment + Visual
- **Без VFX**: всё кроме Niagara, Sequencer, PoseSearch, GAS

### Visual capture

Тулы `capture_viewport`, `capture_from_camera`, `capture_asset_thumbnail`
делают screenshot и встраивают его в tool_result так что Claude
**видит** изображение. Стоит ~2000 input токенов на скриншот.

### Подтверждение действий

Все пишущие инструменты (`set_object_property`, `compile_blueprint`,
любой `pie_*`, любой `add_*`, и т.д.) показывают диалог подтверждения
с параметрами. Можно отключить в `Auto Approve Read-Only Tools`,
но **не рекомендуется**.

### История

Чаты сохраняются автоматически в `Saved/ClaudeAgent/Conversations/`.
Атомарная запись с fsync на каждое событие — переживает крашы
редактора. Левая боковая панель показывает последние беседы.

---

## Настройки (Project Settings → Plugins → Claude Agent)

| Поле | Что делает |
|------|-----------|
| **API Key** | Ключ Anthropic API (только для API backend) |
| **Default Model** | Opus 4.7 / Opus 4.6 / Sonnet 4.6 / Haiku 4.5 |
| **Max Tokens** | Лимит на ответ (1024–64000) |
| **Temperature** | 0.0 = детерминировано, 1.0 = креативно (Opus 4.7 игнорирует) |
| **Stream Responses** | Стриминг по SSE |
| **System Prompt** | Системный промпт (тут же управляется язык) |
| **Max Agent Iterations** | Сколько раз tools могут быть вызваны за один turn |
| **Confirm Before Writes** | Диалог подтверждения для пишущих tools |
| **Auto Approve Read-Only Tools** | Не спрашивать про read-only |
| **Save Conversations** | Сохранять чаты на диск |
| **Backend** | Anthropic API / Claude Code CLI |
| **Enable MCP Server** | HTTP MCP server для CLI |
| **MCP Server Port** | Порт (по умолчанию 17812) |
| **Auto Register With CLI** | Авторегистрация в Claude Code |
| **Tool Filtering** | 22 чекбокса категорий |

---

## Подводные камни

### Контекст-окно

Полный JSON большого Blueprint мотыжит токены. Если получишь ошибку
про `max_tokens` или `context_length` — попроси Claude использовать
`list_blueprint_functions` сначала, потом `get_blueprint_graph` с
конкретным `graph_name`.

`FClaudeContextProvider::Truncate` имеет хард-лимит ~30000 символов
на один результат tool.

### Сохранение в .uasset

Пишущие тулы помечают package как dirty. Сохранение на диск остаётся
за тобой — **File → Save All** или Ctrl+S. Сделано специально, чтобы
не было сюрпризов.

### Реальные ограничения через reflection

Многие тулы (особенно StateTree, Pose Search, PCG, Niagara) построены
на reflection потому что API этих систем меняется между UE-версиями.
Если упадёт runtime ошибка про "field not found" или "function not
found" — это значит UE поменял внутреннее имя. Скажи в чате —
обычно правится одной строкой.

Тулы помечают такие случаи понятными сообщениями типа "feature not
yet supported, use editor instead".

### Безопасность ключа

API key хранится в `Saved/Config/...EditorPerProjectUserSettings.ini`.
**Не должен** попадать в Git. Проверь что `Saved/` в `.gitignore`.

### Совместимость

- Тестировалось на UE 5.7. На 5.4–5.6 должно работать с минимальными
  правками — компилятор покажет несовпадения сигнатур.
- Только Editor-сборка. На рантайме плагин не загружается.

---

## Лицензия

MIT-style для кода плагина. Anthropic API/Claude Code — отдельные
условия Anthropic.

## Логи

`LogClaudeAgent` категория. Подробный режим:

```ini
[/Script/Engine.Engine]
+EngineLogCategoryVerbosity=LogClaudeAgent Verbose
```

в `Config/DefaultEngine.ini`.

---
---

# Claude Agent — Unreal Engine 5.7 Plugin

A full-featured Anthropic Claude AI assistant embedded directly into
the UE5 editor as a dockable panel. Works as an agent: chooses which
of ~200 available tools to call, reads the Asset Registry, parses
Blueprint graphs, inspects logs, modifies properties (with your
confirmation), and compiles BPs.

## Features

- **Two backends**: Anthropic API (pay-per-token) and Claude Code CLI
  (uses your Pro/Max subscription — no API key needed).
- **MCP server** — plugin runs a local MCP server so Claude Code CLI
  can access all UE tools.
- **Dockable editor panel** (Tools → Claude Agent).
- **Streaming responses** via SSE.
- **Tool use loop** — agent autonomously picks tools.
- **Drag-and-drop assets** from Content Browser into chat.
- **Content Browser context menu**: right-click asset →
  *Ask Claude about this asset*.
- **Write confirmation**: any mutating tool requires confirmation
  through a dialog.
- **Project-aware system prompt** — default prompt embeds GASP, UE
  5.7, CMC/Mover branches, and Blueprint-first workflow context.
- **Tool filtering** — 22 categories with on/off toggles. Disable
  categories you don't use to reduce noise and tokens.
- **Sticky conversations** — atomic save on every event, fsync to
  disk. History survives editor crashes.
- **Visual capture** — viewport screenshots / asset thumbnails sent
  directly into Claude's context so the model can actually *see*
  results.

## Supported Systems (~200 tools)

| Category | Count | What it does |
|----------|-------|--------------|
| **Blueprint** | 53 | BP create/edit, graph nodes, K2 nodes, navigation |
| **Animation** | 13 | AnimSequence, Montage, AnimBlueprint |
| **Level / Actor** | 13 | Spawn, transform, level design batches (grid/line/duplicate/align) |
| **PIE** | 12 | Runtime debugging in Play mode (actors, blackboard, console) |
| **PCG** | 12 | Procedural Content Generation (graphs, nodes, volumes) |
| **StateTree** | 12 | Create and edit StateTrees |
| **Behavior Tree** | 12 | BT + Blackboard, decorators, services |
| **Control Rig** | 9 | Read-only Control Rig + IK Retargeter editing |
| **Material** | 8 | Material Instance Constants, parameters |
| **GAS** | 8 | Gameplay Ability System (Abilities, Effects, AttributeSets) |
| **Debug** | 8 | Compile errors, broken refs, orphans, dependency tree |
| **Data Asset** | 7 | DA + DataTable with nested property paths |
| **Spline** | 6 | Create/edit splines |
| **Sequencer** | 6 | Level Sequences, bindings, tracks, keyframes |
| **General** | 6 | Project info, asset search, log reading |
| **Pose Search** | 4 | Schema, Database, Motion Matching |
| **Niagara** | 4 | VFX systems, emitters |
| **Environment** | 4 | Lights, fog, post-process, sky atmosphere |
| **Visual** | 3 | Screenshot viewport, capture from camera, asset thumbnail |

Tool filtering lets you toggle each category individually in settings.
For example, if you're on GASP, you can disable GAS; Pose Search is
rarely needed — disable it too.

---

## Installation

### 1. Copy the plugin into your project

Place the `ClaudeAgent` folder into `<YourProject>/Plugins/`. Result:

```
YourProject/
└── Plugins/
    └── ClaudeAgent/
        ├── ClaudeAgent.uplugin
        ├── Resources/
        └── Source/
            └── ClaudeAgentEditor/
                ├── ClaudeAgentEditor.Build.cs
                ├── Public/
                └── Private/
```

Create the `Plugins` folder if it doesn't exist.

### 2. Regenerate project files

Right-click `<YourProject>.uproject` → **Generate Visual Studio project
files**.

### 3. Build

Open the `.sln`, set configuration to **Development Editor** + **Win64**,
build (`Ctrl+Shift+B`).

CLI alternative:

```bash
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ^
    YourProjectEditor Win64 Development ^
    -Project="C:\Path\To\YourProject.uproject" -WaitMutex
```

### 4. Launch the editor

On first launch UE may ask to build missing modules — agree. If the
plugin isn't enabled by default: **Edit → Plugins → find «Claude
Agent» → checkbox → restart**.

### 5a. Using with the Anthropic API (paid per-token)

1. Get a key at https://console.anthropic.com/ (API Keys section).
2. **Edit → Project Settings → Plugins → Claude Agent**.
3. **API Key** → paste the key.
4. **Default Model** → `claude-opus-4-7` (recommended),
   `claude-sonnet-4-6` (faster/cheaper), or `claude-haiku-4-5`.

The key is stored in `Saved/Config/.../EditorPerProjectUserSettings.ini`
(stays out of Git if `Saved/` is in `.gitignore`).

### 5b. Using with Claude Pro/Max via CLI (free over subscription)

If you have Claude Pro/Max, you can work via the CLI without an API key.

1. Install Node.js 18+ (https://nodejs.org/).
2. In terminal:
   ```
   npm install -g @anthropic-ai/claude-code
   ```
3. Run `claude` once manually → `/login` command → browser auth.
4. **Project Settings → Plugins → Claude Agent**:
   - **Backend** → `Claude Code CLI`
   - **Claude Code Binary Path** → empty (if `claude.cmd` is in PATH)
     or full path like
     `C:\Users\You\AppData\Roaming\npm\claude.cmd`.
5. Press **↻ Reload** in the panel to switch backends.

### 5c. MCP server: connect UE tools to CLI (recommended for CLI)

**Best configuration**: Pro/Max subscription via CLI + the full set
of UE plugin tools. Without the MCP server, CLI only sees project
files on disk.

On editor startup the plugin automatically:
1. Starts an HTTP MCP server at `127.0.0.1:17812/mcp`.
2. Registers it with Claude Code CLI via `claude mcp add`.

Check the Output Log for two lines:
```
LogClaudeAgent: MCP server listening on http://127.0.0.1:17812/mcp
LogClaudeAgent: MCP auto-registered with Claude Code as 'unreal' (user scope)
```

If auto-registration didn't work — press the **MCP** button in the
chat panel toolbar, or manually:
```
claude mcp add unreal --transport http http://127.0.0.1:17812/mcp --scope user
```

Verify: `claude mcp list` → should show `unreal: ✓ Connected`.

**Important:**
- MCP server only works while the editor is open.
- Tools do **not** require confirmation through MCP (CLI protocol
  specific).
- Port is localhost-only — not externally accessible.

### 6. Open the panel

**Tools → Claude Agent**, or via Window menu. The dock tab can be
moved anywhere.

---

## Usage

### Basic chat

Type a question in the bottom field, **Ctrl+Enter** to send.

### Attaching assets

1. **Drag-and-drop** asset(s) from Content Browser into the chat
   window.
2. **Context menu**: right-click asset → *Ask Claude about this asset*.

### Tool filtering

**Project Settings → Plugins → Claude Agent → Tool Filtering** —
22 checkboxes for category enable/disable. Uncheck → tools in that
category aren't sent to the API.

Useful presets:
- **Just project understanding**: General + Blueprint + Animation + Visual
- **AnimBP work**: General + Blueprint + Animation + ControlRig
- **Level design**: General + Level + Spline + PCG + Environment + Visual
- **No VFX**: everything except Niagara, Sequencer, PoseSearch, GAS

### Visual capture

Tools `capture_viewport`, `capture_from_camera`,
`capture_asset_thumbnail` take screenshots and embed them in
tool_result so Claude **sees** the image. Costs ~2000 input tokens
per screenshot.

### Action confirmation

All write tools (`set_object_property`, `compile_blueprint`,
any `pie_*`, any `add_*`, etc.) show a confirmation dialog with
parameters. Can be disabled via `Auto Approve Read-Only Tools` —
**not recommended**.

### History

Chats save automatically to `Saved/ClaudeAgent/Conversations/`.
Atomic write with fsync on every event — survives editor crashes.
Left sidebar shows recent conversations.

---

## Settings (Project Settings → Plugins → Claude Agent)

| Field | What it does |
|-------|--------------|
| **API Key** | Anthropic API key (API backend only) |
| **Default Model** | Opus 4.7 / Opus 4.6 / Sonnet 4.6 / Haiku 4.5 |
| **Max Tokens** | Per-response cap (1024–64000) |
| **Temperature** | 0.0 = deterministic, 1.0 = creative (Opus 4.7 ignores) |
| **Stream Responses** | SSE streaming |
| **System Prompt** | System prompt (also controls language) |
| **Max Agent Iterations** | Tool calls per turn |
| **Confirm Before Writes** | Confirmation dialog for write tools |
| **Auto Approve Read-Only Tools** | Skip confirmation for reads |
| **Save Conversations** | Persist chats to disk |
| **Backend** | Anthropic API / Claude Code CLI |
| **Enable MCP Server** | HTTP MCP server for CLI |
| **MCP Server Port** | Default 17812 |
| **Auto Register With CLI** | Auto-register with Claude Code |
| **Tool Filtering** | 22 category checkboxes |

---

## Caveats

### Context window

Full JSON of a large Blueprint chews tokens. If you get a `max_tokens`
or `context_length` error, ask Claude to use `list_blueprint_functions`
first, then `get_blueprint_graph` with a specific `graph_name`.

`FClaudeContextProvider::Truncate` has a hard limit of ~30000 chars
per tool result.

### Saving .uasset

Write tools mark packages as dirty. Saving to disk is up to you —
**File → Save All** or Ctrl+S. Intentional, to avoid surprises.

### Real-world reflection limits

Many tools (especially StateTree, Pose Search, PCG, Niagara) use
reflection because their APIs shift between UE versions. If you hit
a runtime error like "field not found" or "function not found", it
means UE renamed something internally. Tell me in chat — usually a
one-liner fix.

Tools mark such cases with clear messages like "feature not yet
supported, use editor instead".

### Key safety

API key is stored in `Saved/Config/...EditorPerProjectUserSettings.ini`.
Should **not** end up in Git. Verify `Saved/` is in `.gitignore`.

### Compatibility

- Tested on UE 5.7. Should work on 5.4–5.6 with minimal fixes —
  the compiler will show signature mismatches.
- Editor-only build. Plugin doesn't load at runtime.

---

## License

MIT-style for plugin code. Anthropic API/Claude Code have separate
Anthropic terms.

## Logging

`LogClaudeAgent` category. Verbose mode:

```ini
[/Script/Engine.Engine]
+EngineLogCategoryVerbosity=LogClaudeAgent Verbose
```

in `Config/DefaultEngine.ini`.
