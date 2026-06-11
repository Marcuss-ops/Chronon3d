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

## Quick Start

```bash
# Build everything (vcpkg + cmake — automatic)
bash tools/chronon-linux.sh

# List registered compositions
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli list

# Render a single frame
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render BackgroundGrid --frame 0 -o output/test.png

# Export a video
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli video MyComp --start 0 --end 30 --fps 30 -o output/video.mp4
```

See [ORIENTATION.md](docs/ORIENTATION.md) for a full build guide and architecture overview.

### Prerequisites (Linux)

```bash
# Install build tools
sudo apt-get install -y build-essential cmake ninja-build

# Install ccache for faster rebuilds (optional but recommended)
sudo apt-get install -y ccache
# Configure cache size to your liking (default: 5 GB)
ccache --set-config=max_size=10G
ccache --set-config=compression=true

# ffmpeg runtime (for video export via external process)
sudo apt-get install -y ffmpeg

# Only needed if you enable native FFmpeg: -DCHRONON3D_ENABLE_NATIVE_FFMPEG=ON
# sudo apt-get install -y libswscale-dev libavutil-dev libavcodec-dev libavformat-dev

sudo apt-get install -y zip unzip curl pkg-config
```

> **Windows:** Standard CMake + vcpkg — see [ORIENTATION.md](docs/ORIENTATION.md#windows) for details.

---

## Main CLI Commands

| Command | Description |
|---|---|
| `chronon3d_cli list` | List all registered compositions |
| `chronon3d_cli info <comp>` | Show composition metadata |
| `chronon3d_cli doctor` | Environment diagnostics |
| `chronon3d_cli verify` | Quick render smoke test |
| `chronon3d_cli render <comp> --frame N -o path` | Render a single frame |
| `chronon3d_cli video <comp> --end N -o path` | Export video via ffmpeg |
| `chronon3d_cli bench <comp> --frames 30` | Performance benchmark |
| `chronon3d_cli graph <comp> --frame 0` | Print frame DAG |
| `chronon3d_cli video camera --axis Pan ...` | Render camera motion clips |

> `--report` saves render data to the telemetry database for the web dashboard.

Full CLI reference and all options in [ORIENTATION.md](docs/ORIENTATION.md#cli-reference).

---

## Features at a Glance

| Area | Status |
|---|---|
| **Shapes** | Rect, RoundedRect, Circle, Line, SVG Path |
| **Text** | TTF + HarfBuzz shaping, per-glyph animation, layout presets |
| **Images** | PNG loading, opacity, UV mapping |
| **Layers** | Hierarchical transforms, blend modes, masks, 2.5D camera |
| **Effects** | Blur, Tint, Brightness, Contrast, Glow, DropShadow, Bloom |
| **Animation** | Interpolation, spring physics, keyframes, expressions |
| **Camera 2.5D** | AE-style 3D: Z depth, perspective, parallax, Z-sorting |
| **Video Export** | PNG sequence / ffmpeg pipe (YUV420P, NV12, io_uring) |
| **Performance** | SIMD (Highway), render graph DAG + content-hash caching, TBB parallelism, Huge Pages, Framebuffer Pool |
| **Telemetry** | 30+ counters, SQLite DB, React dashboard |

---

## Register a Composition

```cpp
// your_app/my_comp.cpp
#include <chronon3d/chronon3d.hpp>
using namespace chronon3d;

static Composition MyComp() {
    return composition({ .name = "MyComp", .width = 1920, .height = 1080, .duration = 60 },
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            // ... build your scene ...
            return s.build();
        });
}
CHRONON_REGISTER_COMPOSITION("MyComp", MyComp)
```

The default build includes example compositions, so `chronon3d_cli list` and `chronon3d_cli verify` work out of the box.

---

## Further Reading

| Document | Content |
|---|---|
| **[ORIENTATION.md](docs/ORIENTATION.md)** | Architecture overview, repo structure, build guide, CLI reference, API overview, telemetry dashboard, test instructions |
| **[ROADMAP.md](docs/ROADMAP.md)** | Active roadmap — prioritized items to implement |
| **[IMPROVEMENTS.md](docs/IMPROVEMENTS.md)** | Full improvement backlog with matrix, gap analysis, and V3 blueprint |
| **[V3_BLUEPRINT.md](docs/V3_BLUEPRINT.md)** | Tile-based architecture evolution (frame-based to tile-first) |
| **[CHANGELOG.md](docs/CHANGELOG.md)** | Completed items and release history |
| **[CONTRIBUTING.md](CONTRIBUTING.md)** | Contributor guide |
| **[FEATURES.md](docs/FEATURES.md)** | Detailed feature reference — SVG Path Import V1, Text V1 layout engine & presets, limitations |
| **`docs/improvements/`** | Detailed improvement tiers (game-changers, quick wins, long-term vision) |

---

## Key Architecture (30 seconds)

```
Composition (C++) → Scene → RenderGraph (DAG) → GraphExecutor → SoftwareRenderer → Framebuffer → PNG/MP4
```

| Layer | Role |
|---|---|
| **Composition** | Defines the animated scene in C++ |
| **SceneBuilder** | Fluent API for shapes, layers, camera, effects |
| **RenderGraph** | DAG of render nodes per frame (source, effect, composite) |
| **GraphExecutor** | Executes the DAG with content-hash caching + dirty rect |
| **SoftwareRenderer** | CPU rasterization with SIMD (Highway), Framebuffer Pool |

Headless, deterministic, CPU-only, PMR arena per frame.

---

## License

MIT — see [LICENSE](LICENSE).
