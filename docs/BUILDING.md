# Building Chronon3d

## Prerequisites

| Tool | Minimum version | Notes |
|---|---|---|
| GCC or Clang | GCC 12 / Clang 15 | C++20 required |
| CMake | 3.24 | Preset support required |
| Ninja | any | Generator used by all presets |
| Git | any | Required by vcpkg |
| ffmpeg | any | Required in PATH for video export |

---

## Quick start (Linux)

```bash
# 1. Clone and bootstrap vcpkg (one-time)
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics

# 2. Configure and build
bash tools/chronon-linux.sh
```

## Quick start (Windows)

```powershell
# 1. Clone and bootstrap vcpkg (one-time)
git clone https://github.com/microsoft/vcpkg $env:USERPROFILE\vcpkg
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat" -disableMetrics

# 2. Configure and build
.\tools\chronon-win.ps1
```

That's it. vcpkg reads `vcpkg.json` at the project root and installs every dependency into `vcpkg_installed/` before CMake runs. No manual `vcpkg install` needed.

---

## Build presets

Presets are defined in `CMakePresets.json`.

| Preset | Platform | Use |
|---|---|---|
| `linux-release` | Linux | Production / CI |
| `linux-debug` | Linux | Development |
| `linux-debug-render` | Linux | Fast CLI-only render loop |
| `win-release` | Windows | Production / CI |
| `win-debug` | Windows | Development |
| `win-debug-render` | Windows | Fast CLI-only render loop |

The default CMake build includes the CLI and tests.

```bash
cmake --preset linux-debug
cmake --build build/chronon/linux-debug -j$(nproc)

# Faster render-only loop
cmake --preset linux-debug-render
cmake --build build/chronon/linux-debug-render -j$(nproc) --target chronon3d_cli
```

---

## Running tests

The easiest way to run the test suite is via CTest presets:

```bash
# On Linux
ctest --preset linux-test --output-on-failure

# On Windows
ctest --preset win-test --output-on-failure
```

Or run any of the individual test binaries directly for faster iteration:

```bash
# Example (Linux Release build)
./build/chronon/linux-release/tests/chronon3d_core_tests
./build/chronon/linux-release/tests/chronon3d_core_tests -tc="GraphExecutor*"

# Example (Windows Release build)
.\build\chronon\win-release\tests\chronon3d_core_tests.exe
```

The compiled test binaries reside inside the `tests/` subdirectory of your build folder. The available test suites are:
- `chronon3d_core_tests` — Core tests (Math, Geometry, Animation, Timeline)
- `chronon3d_scene_tests` — Scene build, Layer hierarchy, and SceneBuilder tests
- `chronon3d_renderer_tests` — Renderer, Effects, Render Graph, and lighting tests
- `chronon3d_io_tests` — Core IO and PNG output validity tests
- `chronon3d_cli_tests` — CLI parsing, utilities, and job plan tests

The CMake target `chronon3d_tests` is an aggregate custom target that compiles all of these.

---

## Running the CLI

Run the CLI from the repo root so relative asset paths resolve correctly.

```bash
# List all registered compositions
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli list

# Render a single frame
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render MyComp --frame 0 -o output/frame.png

# Render a frame range
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render MyComp --start 0 --end 90 \
    -o output/frames/frame_####.png

# Export video (requires ffmpeg in PATH)
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli video MyComp \
    --start 0 --end 90 --fps 30 -o output/my_comp.mp4

```

---

## Windows Support

Windows is fully supported via CMake and PowerShell. For the fastest setup, use the `tools/chronon-win.ps1` script as described in the [Quick start](#quick-start-windows).

Manual build via CMake:
```powershell
cmake --preset win-release
cmake --build build/chronon/win-release -j16

# Faster render-only loop
cmake --preset win-debug-render
cmake --build build/chronon/win-debug-render --config Debug --target chronon3d_cli
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

Tests live in `tests/`. Add the new `.cpp` file explicitly to `tests/CMakeLists.txt`. Include `<doctest/doctest.h>`, and write `TEST_CASE` blocks.

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
