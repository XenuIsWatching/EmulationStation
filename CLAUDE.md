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
