# Chronon3d

A **code-first**, headless, CPU-only motion graphics engine written in C++20.
Compose videos the same way you write code — no GUI, no JSON, no keyframe editor.

```cpp
Composition MyVideo() {
    return composition({
        .name = "MyVideo", .width = 1280, .height = 720, .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {interpolate(ctx.frame, 0, 60, -200.0f, 0.0f), 0, -1000},
            .zoom = 1000.0f
        });

        s.layer("card", [&](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0})
             .mask_rounded_rect({ .size = {680, 380}, .radius = 36 })
             .blur(interpolate(ctx.frame, 0, 30, 12.0f, 0.0f));

            l.rounded_rect("bg", {
                .size = {680, 380}, .radius = 36,
                .color = Color{0.1f, 0.14f, 0.28f, 1}
            });
            l.text("title", {
                .content = "CHRONON3D",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf",
                           .size = 64, .color = Color{1,1,1,1},
                           .align = TextAlign::Center }
            });
        });

        return s.build();
    });
}
```

```bash
chronon3d_cli video MyVideo --start 0 --end 90 --fps 30 -o output/my_video.mp4
```

---

## Features

| Area | What's implemented |
|---|---|
| **Shapes** | Rect, RoundedRect, Circle, Line |
| **Text** | TTF rendering, Left/Center/Right alignment, perspective scale |
| **Images** | PNG loading, opacity, UV mapping |
| **Layers** | Hierarchical transforms, opacity, draw order |
| **Masks** | Rect / RoundedRect / Circle per layer, inverted masks |
| **Effects** | Blur (Gaussian approx.), Tint, Brightness, Contrast |
| **Blend modes** | Normal, Add, Multiply, Screen, Overlay |
| **Camera 2.5D** | AE-style Classic 3D — Z depth, perspective scale, parallax, Z-sorting |
| **Animation** | `interpolate()`, `spring()`, `Sequence`, `Easing` presets |
| **Video export** | PNG sequence → MP4 via external `ffmpeg` |
| **Node effects** | Drop shadow, Glow (node-level) |

**Architecture:** CPU-only, headless, deterministic, code-first, PMR arena per frame.

---

## Quick Start

### 1. Build (Linux)

```bash
bash tools/chronon-linux.sh   # install deps via vcpkg + cmake
```

Or with xmake (recommended for development):

```bash
# install xmake once
curl -fsSL https://xmake.io/shget.text | bash

xmake f -m debug
xmake -y
```

### 2. Build (Windows)

```powershell
.\tools\chronon-win.ps1          # vcpkg + cmake
# or
xmake f -m debug
xmake -y
```

See [BUILDING.md](BUILDING.md) for details.
Architecture and planning notes live in [docs/INDEX.md](docs/INDEX.md), [docs/3d_subsystem.md](docs/3d_subsystem.md), [ARCHITECTURE.md](ARCHITECTURE.md), and [ROADMAP.md](ROADMAP.md).

### 3. Register a composition

```cpp
// examples/my_comp.cpp
#include <chronon3d/chronon3d.hpp>
using namespace chronon3d;

static Composition MyComp() { ... }
CHRONON_REGISTER_COMPOSITION("MyComp", MyComp)
```

### 4. Use the CLI

```bash
# list all registered compositions
chronon3d_cli list

# get composition metadata
chronon3d_cli info MyComp

# render a single frame
chronon3d_cli render MyComp --frame 0 -o output/frame.png

# render a frame range
chronon3d_cli render MyComp --start 0 --end 90 -o output/frames/frame_####.png

# export video  (requires ffmpeg in PATH)
chronon3d_cli video MyComp --start 0 --end 90 --fps 30 -o output/my_comp.mp4

# run a built-in proof suite
chronon3d_cli proofs list
chronon3d_cli proofs all -o output/proofs/all
chronon3d_cli proofs camera25d -o output/proofs/camera25d
```

---

## CLI Reference

### `render`
```
chronon3d_cli render <Comp> [--frame N] [--start N] [--end N] [-o path]
```
- `--frame N` — render single frame N
- `--start / --end` — render frame range [start, end)
- `-o path` — output path; use `####` for zero-padded frame number
- `--diagnostic` — overlay debug info

### `video`
```
chronon3d_cli video <Comp> --end N -o output.mp4 [options]
```
| Option | Default | Description |
|---|---|---|
| `--start` | 0 | Start frame (inclusive) |
| `--end` | required | End frame (exclusive) |
| `--fps` | 30 | Output frame rate |
| `--crf` | 18 | x264 quality (0=best, 51=worst) |
| `--preset` | medium | x264 preset |
| `--keep-frames` | off | Keep temporary PNG frames |
| `--frames-dir` | auto | Override temp frames directory |

**Requires `ffmpeg` in PATH.** The engine itself has no FFmpeg dependency.

### `proofs`
```
chronon3d_cli proofs list
chronon3d_cli proofs <suite> [-o output_dir]
chronon3d_cli proofs all -o output/proofs/all
```
Built-in suites: `text`, `layer`, `image`, `effects`, `camera25d`, `masks`

---

## API Overview

### Composition
```cpp
composition({ .name, .width, .height, .duration }, [](const FrameContext& ctx) {
    SceneBuilder s(ctx);
    // ...
    return s.build();
});
```

### Shapes (root level)
```cpp
s.rect("id",         { .size, .color, .pos });
s.rounded_rect("id", { .size, .radius, .color, .pos });
s.circle("id",       { .radius, .color, .pos });
s.line("id",         { .from, .to, .thickness, .color });
s.text("id",         { .content, .style, .pos });
s.image("id",        { .path, .size, .pos, .opacity });
```

### Layers
```cpp
s.layer("name", [](LayerBuilder& l) {
    l.position({x, y, z})
     .scale({sx, sy, 1})
     .rotate({0, 0, deg})
     .opacity(alpha)
     .enable_3d(true)                           // Camera 2.5D
     .mask_rounded_rect({ .size, .radius })     // Mask
     .blur(radius)                              // Effects
     .tint(Color{r, g, b, strength})
     .brightness(value)
     .contrast(value)
     .blend(BlendMode::Screen);

    l.rect(...);
    l.image(...);
    l.text(...);
});
```

### Camera 2.5D
```cpp
s.camera_2_5d({
    .enabled          = true,
    .position         = {cam_x, cam_y, -1000},
    .point_of_interest = {0, 0, 0},
    .zoom             = 1000.0f
});
```
Convention: Z negative = closer (larger), Z positive = farther (smaller).

### Animation
```cpp
auto x  = interpolate(ctx.frame, 0, 60, 0.0f, 800.0f, Easing::InOutCubic);
auto [v, vel] = spring(ctx.frame, target, { .stiffness = 120, .damping = 14 });
```

---

## Coordinate System

- Origin (0, 0) — top-left
- X increases right, Y increases down
- Z negative — closer to camera (Camera 2.5D)
- Shapes are **center-anchored** by default
- Draw order — painter's algorithm (insertion order for 2D, depth-sorted for 3D layers)

---

## Dependencies

| Library | Purpose |
|---|---|
| glm | Math (Vec, Mat, Quat) |
| stb_truetype | Font rasterization |
| stb_image | PNG/JPG loading |
| spdlog + fmt | Logging |
| Google Highway | SIMD blending |
| Taskflow | Parallel frame pipeline |
| CLI11 | CLI argument parsing |
| doctest | Tests |
| **ffmpeg** (external) | MP4 encoding — must be in PATH, not linked |

---

## Project Structure

| Path | Content |
|---|---|
| `apps/chronon3d_cli/` | CLI entry point and commands |
| `examples/` | Registered compositions and proofs |
| `include/chronon3d/` | Public engine headers |
| `src/` | Renderer, scene, IO implementations |
| `tests/` | Automated tests (doctest) |
| `tools/` | Build scripts (`chronon-linux.sh`, `chronon-win.ps1`) |
| `assets/` | Test fonts and images |
| `output/` | Generated files — gitignored |
