# Building Chronon3d

## Prerequisites

| Tool | Minimum version | Notes |
|---|---|---|
| GCC or Clang | GCC 12 / Clang 15 | C++20 required |
| CMake | 3.24 | Preset support required |
| Ninja | any | Generator used by all presets |
| Git | any | Required by vcpkg |
| ffmpeg | any | CLI `video` command only — not linked into the engine |

---

## Quick start (Linux)

```bash
# 1. Clone and bootstrap vcpkg (one-time)
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics

# 2. Export VCPKG_ROOT for this shell (add to ~/.bashrc / ~/.zshrc to persist)
export VCPKG_ROOT=~/vcpkg

# 3. Configure and build
bash tools/chronon-linux.sh
```

That's it. vcpkg reads `vcpkg.json` at the project root and installs every dependency into `vcpkg_installed/` before CMake runs. No manual `vcpkg install` needed.

---

## Build presets

Presets are defined in `CMakePresets.json`.

| Preset | Platform | Use |
|---|---|---|
| `linux-release` | Linux | Production / CI |
| `linux-debug` | Linux | Development |
| `win-release` | Windows | Production / CI |
| `win-debug` | Windows | Development |

The default CMake build includes the CLI and tests.

```bash
cmake --preset linux-debug
cmake --build build/chronon/linux-debug -j$(nproc)
```

---

## Running tests

```bash
ctest --test-dir build/chronon/linux-release --output-on-failure
```

Or run the binary directly for faster iteration:

```bash
./build/chronon/linux-release/chronon3d_tests
./build/chronon/linux-release/chronon3d_tests -tc="GraphExecutor*"
```

---

## Running the CLI

```bash
# List all registered compositions
./build/chronon/linux-release/chronon3d_cli list

# Render a single frame
./build/chronon/linux-release/chronon3d_cli render MyComp --frame 0 -o output/frame.png

# Render a frame range
./build/chronon/linux-release/chronon3d_cli render MyComp --start 0 --end 90 \
    -o output/frames/frame_####.png

# Export video (requires ffmpeg in PATH)
./build/chronon/linux-release/chronon3d_cli video MyComp \
    --start 0 --end 90 --fps 30 -o output/my_comp.mp4

```

---

## Windows

```powershell
# 1. Clone and bootstrap vcpkg
git clone https://github.com/microsoft/vcpkg $env:USERPROFILE\vcpkg
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
$env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"

# 2. Build from a Visual Studio Developer Command Prompt
.\tools\chronon-win.ps1
```

The script opens the Visual Studio build environment, configures CMake with the `win-release` preset, and builds the CLI into:

```text
build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe
```

If you want the debug build, run:

```powershell
.\tools\chronon-win.ps1 -Configuration Debug
```

Run the CLI from the repo root so relative asset paths resolve correctly:

```powershell
.\build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe list
```

---

## Troubleshooting

### `vcpkg install` fails with "baseline not found"

The clone was shallow. Run:

```bash
git -C ~/vcpkg fetch --unshallow
```

### Ninja not found by CMake

Add your Ninja binary directory to PATH before running cmake:

```bash
export PATH="/path/to/ninja/bin:$PATH"
```

On Linux, `ninja-build` is the apt package name:

```bash
sudo apt install ninja-build
```

### `xxhash` not found

Check the case: it must be `xxHash` (capital H) in `find_package`. The package name in vcpkg is case-sensitive on Linux.

### `tomlplusplus` not found

It must be present in both `vcpkg.json` and `CMakeLists.txt`. If you added it only to one, add it to the other and re-run `cmake --preset linux-release`.

---

## Adding a composition

1. Create a source file in your application or module
2. Include `<chronon3d/chronon3d.hpp>`
3. Define a function returning `Composition`
4. Register it with `CHRONON_REGISTER_COMPOSITION("MyComp", MyComp)`
5. Rebuild - the translation unit registers itself when linked

```cpp
#include <chronon3d/chronon3d.hpp>
using namespace chronon3d;

static Composition MyComp() {
    return composition(
        { .name = "MyComp", .width = 1280, .height = 720, .duration = 60 },
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", { .size = {1280, 720}, .color = Color::black(), .pos = {640, 360, 0} });
            return s.build();
        }
    );
}
CHRONON_REGISTER_COMPOSITION("MyComp", MyComp)
```

---

## Adding a test

Tests live in `tests/` and are picked up by glob. Create a new `.cpp` file, include `<doctest/doctest.h>`, and write `TEST_CASE` blocks. No CMake changes needed.

```cpp
#include <doctest/doctest.h>
#include <chronon3d/render_graph/render_graph.hpp>

TEST_CASE("my test") {
    CHECK(1 + 1 == 2);
}
```

---

## Dependency list

All dependencies are managed by vcpkg via `vcpkg.json`. They are built from source and installed into `vcpkg_installed/` (gitignored).

| Package | Version pinned in baseline | Purpose |
|---|---|---|
| glm | via baseline | Math — Vec, Mat, Quat |
| spdlog + fmt | via baseline | Logging |
| xxhash | via baseline | Fast hashing for cache keys |
| taskflow | via baseline | Parallel frame pipeline |
| highway | via baseline | SIMD pixel blending |
| meshoptimizer | via baseline | Mesh utilities |
| concurrentqueue | via baseline | Lock-free queues |
| stb | via baseline | Image load/write, font rasterization |
| cli11 | via baseline | CLI argument parsing |
| doctest | via baseline | Test framework |
| tomlplusplus | via baseline | TOML config parsing in CLI |
| tracy | via baseline | Profiling (optional, off by default) |
