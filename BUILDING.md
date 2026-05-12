# Building Chronon3d

## Prerequisites

| Tool | Required for |
|---|---|
| C++20 compiler (GCC 12+ / Clang 15+ / MSVC 2022+) | Core build |
| xmake OR cmake + ninja | Build system |
| ffmpeg (in PATH) | `chronon3d_cli video` only — not linked into the engine |

---

## Method 1: xmake (recommended for development)

xmake manages all C++ dependencies automatically.

### Install xmake (once)

**Linux / macOS:**
```bash
curl -fsSL https://xmake.io/shget.text | bash
source ~/.xmake/profile
```

**Windows:** Download from https://xmake.io or `winget install xmake`

### Build

```bash
xmake f -m debug          # configure (debug)
xmake f -m release        # configure (release)
xmake -y                  # build (installs deps on first run)
```

### Run

```bash
xmake run -w . chronon3d_cli list
xmake run -w . chronon3d_cli render ImageProof --frame 0 -o output/image.png
xmake run -w . chronon3d_cli proofs all -o output/proofs/all
xmake run -w . chronon3d_cli video Camera25DParallaxProof --start 0 --end 90 --fps 30 -o output/parallax.mp4
```

### Tests

```bash
xmake run chronon3d_tests
```

---

## Method 2: CMake + vcpkg

CMake is the production build path. vcpkg manages dependencies.

### Linux

```bash
# Install vcpkg once
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg

# Build
bash tools/chronon-linux.sh
```

### Windows

```powershell
.\tools\chronon-win.ps1           # Release (default)
.\tools\chronon-win.ps1 -Configuration Debug
```

### CMake manual

```bash
cmake --preset linux-release
cmake --build --preset linux
```

---

## Workflow

### Render a single frame

```bash
chronon3d_cli render MyComp --frame 0 -o output/frame.png
```

### Render a frame range

```bash
chronon3d_cli render MyComp --start 0 --end 90 -o output/frames/frame_####.png
```

Creates `frame_0000.png` … `frame_0089.png` in `output/frames/`.

### Export video (requires ffmpeg)

```bash
chronon3d_cli video MyComp --start 0 --end 90 --fps 30 -o output/my_comp.mp4
```

Options: `--crf 18`, `--preset medium`, `--keep-frames`, `--frames-dir <path>`.

**ffmpeg** must be installed separately and available in `PATH`. It is not linked into the engine.

### Run proof suites

```bash
chronon3d_cli proofs list
chronon3d_cli proofs image    -o output/proofs/image
chronon3d_cli proofs masks    -o output/proofs/masks
chronon3d_cli proofs camera25d -o output/proofs/camera25d
chronon3d_cli proofs all      -o output/proofs/all
```

---

## Directory Structure

| Path | Content |
|---|---|
| `apps/chronon3d_cli/` | CLI source |
| `examples/` | Compositions and proof compositions |
| `include/chronon3d/` | Public headers |
| `src/` | Renderer, scene, IO |
| `tests/` | doctest test suite |
| `tools/` | Build helper scripts |
| `assets/` | Fonts and test images |
| `output/` | Generated output — gitignored |
| `docs/` | Internal design notes |

---

## Adding a Composition

1. Create `examples/my_comp.cpp`
2. Include `<chronon3d/chronon3d.hpp>`
3. Define a function returning `Composition`
4. Register it with `CHRONON_REGISTER_COMPOSITION("MyComp", MyComp)`
5. Rebuild — no CMake/xmake changes needed (glob picks it up)

```cpp
#include <chronon3d/chronon3d.hpp>
using namespace chronon3d;

static Composition MyComp() {
    return composition({ .name = "MyComp", .width = 1280, .height = 720, .duration = 60 },
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", { .size = {1280, 720}, .color = Color::black(), .pos = {640, 360, 0} });
            return s.build();
        });
}
CHRONON_REGISTER_COMPOSITION("MyComp", MyComp)
```
