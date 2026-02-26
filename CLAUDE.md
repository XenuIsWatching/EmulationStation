# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Standard build (outputs emulationstation binary to project root)
cmake .
make -j$(nproc)

# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug .
make -j$(nproc)

# Common build flags
# -DGLES=ON        Embedded OpenGL (Raspberry Pi / mobile)
# -DRPI=ON         Raspberry Pi defaults
# -DOMX=ON         OMXPlayer video support
# -DCEC=ON         HDMI CEC support
# -DPROFILING=ON   Enable profiling

# Linux dependencies (Debian/Ubuntu)
sudo apt-get install libsdl2-dev libfreeimage-dev libfreetype6-dev libcurl4-openssl-dev \
  rapidjson-dev libasound2-dev libgles2-mesa-dev build-essential cmake \
  fonts-droid-fallback libvlc-dev libvlccore-dev vlc-bin
```

## Running (Development)

```bash
./emulationstation --windowed --debug --resolution 1280 720
```

## Testing

There are no application-level unit tests. Only the bundled pugixml library has tests:

```bash
cd external/pugixml
cmake . -DPUGIXML_BUILD_TESTS=ON
make
ctest --output-on-failure
```

---

## Architecture

The project is split into two CMake targets:

- **`es-core/`** — Reusable UI framework, compiles to `libes-core.a`
- **`es-app/`** — Application logic, links es-core and produces the `emulationstation` binary

### Data Layer

XML is parsed throughout using **pugixml** (bundled in `external/pugixml/`).

- **Settings** (`es-core/src/Settings.cpp`) — Reads/writes flat key-value `es_settings.cfg`
- **Gamelists** (`es-app/src/Gamelist.cpp`) — Per-system `gamelist.xml` files mapping ROMs to metadata. Writes are done asynchronously: XML is serialized to a string on the main thread, then written to disk via `std::async`. Call `waitForGamelistWrites()` before shutdown.
- **Themes** (`es-core/src/ThemeData.cpp`) — Parses `theme.xml` files that control visual layout
- **System config** (`es-app/src/SystemData.cpp`) — Reads `es_systems.cfg` to enumerate platforms

### UI Framework (es-core)

All UI elements inherit from `GuiComponent` (`es-core/src/GuiComponent.h`). Key overrides:

- `input(InputConfig*, Input)` — Handle button presses; check with `config->isMappedTo("a", input)`
- `update(int deltaTime)` — Per-frame logic; deltaTime in milliseconds
- `render(const Transform4x4f& parentTrans)` — Draw; apply `parentTrans * getTransform()` then call `renderChildren()`

`Window` (`es-core/src/Window.cpp`) owns a stack of `GuiComponent`s — push to show dialogs, pop to dismiss. It drives the render loop.

The renderer is abstracted behind `Renderer` (`es-core/src/renderers/`) with four backends: GL1.4, GL2.1, GLES1.0, GLES2.0. Vertex positions and transform translations must be **rounded** before rendering to avoid sub-pixel artifacts.

### Async Texture Loading

Texture loading is deliberately non-blocking to prevent NAS/disk I/O from freezing the UI during scrolling.

- `TextureResource::get(path, tile, forceLoad, dynamic, block=false)` — `block=false` queues async load
- `TextureLoader` (background thread) processes a queue via `std::condition_variable`
- `TextureDataManager` maintains an LRU cache; evicts least-recently-used textures when VRAM budget is exceeded
- Components poll `mTexture->updateTextureSize()` in their `update()` loop to detect when loading finishes
- `ImageComponent::setImageAsync(path)` is the main entry point for async image loading

### Application Views (es-app)

- `ViewController` (`es-app/src/views/ViewController.cpp`) — Central navigator; manages camera transitions between `SystemView` and game list views
- `SystemView` — The system carousel/selector
- Four `IGameListView` implementations: `BasicGameListView`, `DetailedGameListView`, `GridGameListView`, `VideoGameListView`
- `CollectionSystemManager` — Virtual collections (Favorites, Last Played, Custom)

### Data Model

- `SystemData` — One instance per console/platform; holds the root `FileData` tree and theme
- `FileData` — Tree node representing a game or folder; carries `MetaData` (title, rating, images, play count, etc.)
- `MetaData` schema is defined in `es-app/src/MetaData.cpp`

### Threading Utilities

- `ThreadPool` (`es-core/src/utils/ThreadPool.h`) — Creates `hardware_concurrency() - 1` worker threads; `queueWorkItem()` to submit, `wait()` to block until all complete
- `AsyncHandle` (`es-core/src/AsyncHandle.h`) — Base class for trackable async operations with states: `ASYNC_IN_PROGRESS`, `ASYNC_ERROR`, `ASYNC_DONE`

---

## Themes

Full reference: `THEMES.md`. Core parsing: `es-core/src/ThemeData.cpp` / `ThemeData.h`.

### Theme XML Structure

```xml
<theme>
  <formatVersion>6</formatVersion>        <!-- required; current version is 6 -->
  <resolution>1920 1080</resolution>      <!-- optional; enables pixel-based coordinates -->
  <variables>...</variables>
  <include>./other.xml</include>
  <feature supported="video">...</feature>
  <view name="basic, detailed, video, grid">
    <image name="background" extra="true">
      <path>./art/bg.png</path>
      <pos>0 0</pos>
      <size>1920 1080</size>
    </image>
  </view>
</theme>
```

- **`resolution`**: When set (e.g. `1920 1080`), all `RESOLUTION_*` values are divided by those dimensions to produce normalized 0–1 coordinates. Without it, values are already 0–1 fractions.
- **`include`**: Merges another theme file (relative path). Parent file values override included values.
- **`feature`**: Block only parsed if the feature is in the supported set (`video`, `carousel`, `z-index`, `visible`).
- **`extra="true"`**: Marks elements as decorative; instantiated by `ThemeData::makeExtras()` (only `image` and `text` types supported).
- Multiple views/elements can be targeted with comma-separated names: `<view name="basic, detailed">`, `<text name="md_lbl_rating, md_lbl_developer">`.

### Variables & System Placeholders

```xml
<variables>
  <themeColor>8b0000</themeColor>
  <artFolder>./art</artFolder>
  <themeFont>${artFolder}/Cabin-Bold.ttf</themeFont>  <!-- variables can reference each other -->
</variables>
```

Built-in system variables (injected per-system at load time):
- `${system.name}` — short name (e.g. `snes`)
- `${system.fullName}` — display name (e.g. `Super Nintendo`)
- `${system.theme}` — theme folder name from `es_systems.cfg`

Variables are resolved recursively by `ThemeData::resolvePlaceholders()` before values are used.

### Supported Views

`system`, `basic`, `detailed`, `grid`, `video`

### Supported Element Types & Key Properties

| Element | Notable Properties |
|---|---|
| `image` | `pos`, `size`, `maxSize`, `origin`, `path`, `default`, `tile`, `color`, `colorEnd`, `gradientType`, `rotation`, `zIndex`, `visible` |
| `text` | `pos`, `size`, `origin`, `text`, `color`, `backgroundColor`, `fontPath`, `fontSize`, `alignment`, `forceUppercase`, `lineSpacing`, `zIndex`, `visible` |
| `textlist` | `pos`, `size`, `primaryColor`, `secondaryColor`, `selectedColor`, `selectorColor`, `selectorImagePath`, `fontPath`, `fontSize`, `alignment`, `horizontalMargin`, `lineSpacing`, `scrollSound`, `zIndex` |
| `imagegrid` | `pos`, `size`, `margin`, `padding`, `autoLayout`, `autoLayoutSelectedZoom`, `imageSource`, `scrollDirection`, `centerSelection`, `scrollLoop`, `animate`, `zIndex` |
| `gridtile` | `size`, `padding`, `imageColor`, `backgroundImage`, `backgroundColor`, `backgroundCornerSize`, `backgroundCenterColor`, `backgroundEdgeColor` |
| `video` | `pos`, `size`, `maxSize`, `origin`, `default`, `delay`, `showSnapshotNoVideo`, `showSnapshotDelay`, `rotation`, `zIndex`, `visible` |
| `rating` | `pos`, `size`, `origin`, `filledPath`, `unfilledPath`, `color`, `rotation`, `zIndex`, `visible` |
| `datetime` | `pos`, `size`, `origin`, `color`, `fontPath`, `fontSize`, `format`, `displayRelative`, `alignment`, `zIndex`, `visible` |
| `carousel` | `type` (`horizontal`/`vertical`/`horizontal_wheel`/`vertical_wheel`), `pos`, `size`, `color`, `colorEnd`, `logoSize`, `logoScale`, `logoRotation`, `maxLogoCount`, `scrollSound`, `zIndex` |
| `helpsystem` | `pos`, `origin`, `textColor`, `iconColor`, `fontPath`, `fontSize` |
| `sound` | `path` (WAV only) |
| `ninepatch` | `pos`, `size`, `path` (48×48px image with 16×16px patches) |
| `container` | `pos`, `size`, `origin`, `visible`, `zIndex` |

Metadata elements in `detailed`/`video` views use `md_` prefix: `md_image`, `md_thumbnail`, `md_marquee`, `md_video`, `md_name`, `md_description`, `md_rating`, `md_releasedate`, `md_developer`, `md_publisher`, `md_genre`, `md_players`, `md_playcount`, `md_lastplayed`. Corresponding labels use `md_lbl_` prefix.

### Property Types

| Type | Format | Example |
|---|---|---|
| `RESOLUTION_PAIR` | two numbers (pixels if resolution set) | `960 540` |
| `NORMALIZED_PAIR` | two 0–1 floats | `0.5 0.5` |
| `RESOLUTION_FLOAT` | one number | `32` |
| `RESOLUTION_RECT` | four numbers | `10 10 10 10` |
| `COLOR` | 6 or 8 hex digits (RGBA) | `FF0000FF` |
| `PATH` | file path; `~` and `./` supported | `./art/bg.png` |
| `BOOLEAN` | `true`/`false`/`1`/`0` | `true` |
| `FLOAT` | decimal | `1.5` |
| `STRING` | text | `horizontal` |

### Z-Index Defaults

Lower values render first (behind). Default z-indices by layer:
- `0` — backgrounds
- `10` — extra elements
- `20` — gamelist / imagegrid
- `30` — main media (image/video)
- `35` — thumbnail / marquee
- `40` — metadata panel; carousel (`system` view)
- `50` — logo / system info text

### Theme File Lookup Order (per system)

1. `[ROM_DIR]/theme.xml`
2. `~/.emulationstation/themes/[THEME_SET]/[SYSTEM]/theme.xml`
3. `/etc/emulationstation/themes/[THEME_SET]/[SYSTEM]/theme.xml`
4. `[THEME_SET]/theme.xml` (fallback)

### How Themes Wire into the UI

**Loading** — At startup, `SystemData::loadTheme()` calls `ThemeData::loadFile()` which parses the XML into `mViews` (a map of view name → element map).

**Application flow** — When a game list view is created or the theme changes:

```
ViewController::getGameListView(system)
  └─ view->setTheme(system->getTheme())
       └─ IGameListView::onThemeChanged(theme)  [virtual dispatch]
            ├─ ISimpleGameListView::onThemeChanged()
            │    ├─ mBackground.applyTheme(theme, view, "background", ALL)
            │    ├─ mHeaderImage.applyTheme(theme, view, "logo", ALL)
            │    ├─ mHeaderText.applyTheme(theme, view, "logoText", ALL)
            │    ├─ delete old mThemeExtras
            │    └─ mThemeExtras = ThemeData::makeExtras(theme, view, window)
            │         └─ for each extra="true" element:
            │              create ImageComponent or TextComponent
            │              component->applyTheme(theme, view, name, ALL)
            └─ (concrete view, e.g. DetailedGameListView)
                 ├─ mList.applyTheme(theme, view, "gamelist", ALL)
                 ├─ mThumbnail.applyTheme(..., "md_thumbnail", POSITION|SIZE|Z_INDEX|ROTATION|VISIBLE)
                 ├─ mImage.applyTheme(..., "md_image", ...)
                 └─ for each metadata label/value:
                      component->applyTheme(theme, view, "md_lbl_developer", ALL)
```

**`ThemeFlags` bitmask** — Controls which properties `applyTheme()` processes:

```cpp
// es-core/src/ThemeData.h
namespace ThemeFlags {
  PATH=1, POSITION=2, SIZE=4, ORIGIN=8, COLOR=16,
  FONT_PATH=32, FONT_SIZE=64, SOUND=128, ALIGNMENT=256,
  TEXT=512, FORCE_UPPERCASE=1024, LINE_SPACING=2048,
  DELAY=4096, Z_INDEX=8192, ROTATION=16384, VISIBLE=32768,
  ALL=0xFFFFFFFF
}
```

Each component's `applyTheme()` checks the bitmask before reading each property from the `ThemeElement`. This lets views apply only a subset (e.g. position + size only) while leaving other properties at their programmatic defaults.

**Key component implementations:**
- `GuiComponent::applyTheme()` — handles `pos`, `size`, `origin`, `rotation`, `zIndex`, `visible` for all components
- `ImageComponent::applyTheme()` — adds `path`, `tile`, `color`/`colorEnd`/`gradientType`, `maxSize`/`minSize`; passes `SIZE^` to base to avoid double-application
- `TextComponent::applyTheme()` — adds `color`, `backgroundColor`, `text`, `alignment`, `forceUppercase`, `lineSpacing`, font
- `TextListComponent::applyTheme()` — adds selector colors, scroll sound, alignment, margins, font
- `Font::getFromTheme(elem, properties, fallback)` — helper used by text components to resolve `fontPath`/`fontSize` with fallback
