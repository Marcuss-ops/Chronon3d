# Chronon3D Tutorial

> A step-by-step guide to building motion graphics with Chronon3D.
> For quick copy-paste snippets, see [`QUICKSTART.md`](QUICKSTART.md).
> For CLI flags, see [`CLI_REFERENCE.md`](CLI_REFERENCE.md).

---

## Prerequisites

Chronon3D is a C++20, headless, CPU-first motion graphics engine.
You need:

```bash
# Build tools
sudo apt-get install -y build-essential cmake ninja-build

# ccache for fast incremental builds (recommended)
sudo apt-get install -y ccache

# ffmpeg for video export
sudo apt-get install -y ffmpeg
```

The build uses [vcpkg](https://github.com/microsoft/vcpkg) for dependency
management. The helper script `tools/chronon-linux.sh` bootstraps everything
automatically, or you can set `VCPKG_ROOT` yourself:

```bash
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
```

---

## 1. Building the Project

### Quick build (recommended for daily work)

```bash
# Clone the repo
git clone <repo-url> chronon3d
cd chronon3d

# First build: configures cmake, bootstraps ccache, populates tmpfs build dir
./build-fast.sh

# Subsequent builds: incremental, ~13-17 seconds on warm ccache + tmpfs
./build-fast.sh
```

### Manual build

```bash
# Configure
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

# Build
cmake --build build --target chronon3d_cli
```

### Verify the build

```bash
# List registered compositions
./build/apps/chronon3d_cli/chronon3d_cli list

# Check environment
./build/apps/chronon3d_cli/chronon3d_cli doctor
```

---

## 2. Your First Composition

Chronon3D programs are **compositions** — self-contained scenes that produce
a `Scene` for each frame. Think of them as the equivalent of a React
component in Remotion.

### Minimal example (~30 lines)

```cpp
#include <chronon3d/project.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/text/text_definition.hpp>

namespace c3d = chronon3d;

int main() {
    // 1. Create a project
    c3d::Project project;
    project.name          = "My First Render";
    project.default_width = 1920;
    project.default_height = 1080;

    // 2. Register a composition
    project.composition("TitleCard",
        {.duration = c3d::Frame{1}},
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);

            // Background layer
            s.layer("bg", [](c3d::LayerBuilder& l) {
                l.fill(c3d::Color{0.1f, 0.1f, 0.15f, 1.0f});
            });

            // Text layer
            s.layer("title", [&ctx](c3d::LayerBuilder& l) {
                l.text("t", c3d::TextDefinition{
                    .content = {.value = "Hello, Chronon3D!"},
                    .style   = {
                        .font = {
                            .font_path   = "fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size   = 72.0f,
                        },
                        .color = c3d::Color::white(),
                    },
                    .frame = {
                        .size = {
                            static_cast<float>(ctx.width),
                            static_cast<float>(ctx.height),
                        },
                        .align = c3d::TextAlign::Center,
                        .vertical_align = c3d::VerticalAlign::Middle,
                    },
                });
            });

            return s.build();
        });

    // 3. Render
    c3d::sdk::RenderSettings settings{
        .width = 1920, .height = 1080, .deterministic = true};
    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root("assets");

    auto result = engine.render(
        project.create("TitleCard"), c3d::sdk::Frame{0});
    if (!result) {
        std::fprintf(stderr, "Render failed\n");
        return 1;
    }
    std::printf("Rendered %dx%d\n", result->width, result->height);
    return 0;
}
```

### CMakeLists.txt for your project

```cmake
cmake_minimum_required(VERSION 3.27)
project(my_project VERSION 1.0.0 LANGUAGES CXX)

find_package(Chronon3D CONFIG REQUIRED)

add_executable(my_render main.cpp)
set_target_properties(my_render PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(my_render PRIVATE Chronon3D::SDK)
```

Build and run:

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/chronon3d-install
cmake --build .
./my_render
```

---

## 3. Working with Text

Text is the primary feature of Chronon3D. There are two APIs:

### Quick presets (recommended for common cases)

```cpp
#include <chronon3d/presets/text/text_presets_v1.hpp>

using namespace chronon3d::presets::text;

// In your composition lambda:
s.text("title", title_centered("My Title"));           // 96pt, centered
s.text("sub",   subtitle_bottom("Subtitle text"));     // 48pt, bottom
s.text("cap",   caption_safe_area("Caption"));         // 36pt, safe area
s.text("hero",  kinetic_word("IMPACT", 144.0f, Color::red())); // kinetic
s.text("lower", lower_third("Presenter Name"));        // 42pt, bottom-left
```

### Custom text with TextDefinition

For full control over font, layout, color, and effects:

```cpp
s.layer("custom", [](LayerBuilder& l) {
    auto def = from_text_spec(TextSpec{
        .content  = {.value = "Custom styled text"},
        .font     = {
            .font_path   = "fonts/Roboto-Light.ttf",
            .font_family = "Roboto",
            .font_weight = 300,
            .font_size   = 64.0f,
        },
        .layout   = {
            .box       = Vec2{1400.0f, 200.0f},
            .anchor    = TextAnchor::Center,
            .align     = TextAlign::Center,
            .tracking  = 8.0f,
            .line_height = 1.4f,
        },
        .position   = Vec3{960.0f, 540.0f, 0.0f},
        .appearance = {.color = Color{0.9f, 0.9f, 0.95f, 1.0f}},
    });
    l.text("label", def);
});
```

### Text with effects

```cpp
s.layer("glow_text", [](LayerBuilder& l) {
    auto def = kinetic_word("HERO", 120.0f, Color{1.0f, 0.8f, 0.2f});
    def.effects.glow = GlowParams{
        .color     = Color{1.0f, 0.6f, 0.1f, 1.0f},
        .radius    = 24.0f,
        .intensity = 0.8f
    };
    l.text("label", def);
});
```

### Text animation (typewriter)

```cpp
#include <chronon3d/text/text_run_spec.hpp>

s.layer("typewriter", [&ctx](LayerBuilder& l) {
    l.text_run("reveal", TextRunSpec{
        .content  = {.value = "Typewriter reveal..."},
        .font     = {.font_size = 64.0f},
        .layout   = {
            .box = Vec2{1200.0f, 200.0f},
            .anchor = TextAnchor::Center,
            .align  = TextAlign::Center,
        },
        .position = Vec3{960.0f, 540.0f, 0.0f},
        .animator = TypewriterAnimator{
            .start_delay = 0,
            .chars_per_second = 12.0f,
            .cursor_blink = true
        }
    });
});
```

---

## 4. Layers and Compositing

Scenes are built from **layers** — independent visual elements stacked
in order.

### Solid color fills

```cpp
s.layer("background", [](LayerBuilder& l) {
    l.fill(Color{0.05f, 0.05f, 0.1f, 1.0f});
});
```

### Images

```cpp
s.layer("image", [](LayerBuilder& l) {
    l.image("logo", asset("images/logo.png"));
});
```

### Multiple layers (z-order = add order)

```cpp
s.layer("bg",     [](LayerBuilder& l) { l.fill(Color::black()); });
s.layer("image",  [](LayerBuilder& l) { l.image("photo", asset("photo.jpg")); });
s.layer("title",  [](LayerBuilder& l) {
    l.text("t", title_centered("MY PROJECT"));
});
s.layer("overlay", [](LayerBuilder& l) {
    l.fill(Color{1.0f, 1.0f, 1.0f, 0.1f}); // semi-transparent white
});
```

---

## 5. Animation

Chronon3D animations are **frame-driven** — you receive a `FrameContext`
with the current frame number, and your composition produces the correct
`Scene` for that frame.

### Composing an animated composition

```cpp
project.composition("FadeIn",
    {.duration = c3d::Frame{90}},  // 3 seconds at 30fps
    [](const c3d::FrameContext& ctx) -> c3d::Scene {
        c3d::SceneBuilder s(ctx);

        // Compute opacity based on current frame
        float t = static_cast<float>(ctx.frame.value) / 90.0f;
        float opacity = std::clamp(t, 0.0f, 1.0f);

        s.layer("text", [&ctx, opacity](LayerBuilder& l) {
            l.text("t", c3d::TextDefinition{
                .content = {.value = "Fading In"},
                .style   = {
                    .font  = {.font_size = 96.0f},
                    .color = Color{1.0f, 1.0f, 1.0f, opacity},
                },
                .frame = {
                    .size = {float(ctx.width), float(ctx.height)},
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                },
            });
        });

        return s.build();
    });
```

### Animation helpers

Chronon3D provides 17 inline animation helpers in
`<chronon3d/animation/interpolate.hpp>`:

```cpp
#include <chronon3d/animation/interpolate.hpp>

// Smooth interpolation between values
float x = interpolate(ctx.frame, Frame{0}, Frame{30}, 0.0f, 100.0f);

// Spring physics
float y = spring(ctx.frame, Frame{0}, Frame{45}, 0.0f, 200.0f);

// Stagger delays for multiple elements
float delay = stagger(i, total, 5);  // 5 frames between each element
```

---

## 6. Using the CLI

The `chronon3d_cli` tool renders compositions without writing any C++ code.

### Render a still image

```bash
chronon3d_cli render BackgroundGrid --frame 0 -o output/test.png
```

### Render a sequence (frames 0 through 90)

```bash
chronon3d_cli render BackgroundGrid --frames 0-90 -o output/frame_####.png
```

### Render a video (output extension selects format)

```bash
chronon3d_cli render BackgroundGrid -o output/background_grid.mp4
```

### Inspect text audit

```bash
chronon3d_cli inspect-text HelloWorld --frame 0 --json
```

### List all registered compositions

```bash
chronon3d_cli list
```

### Check environment

```bash
chronon3d_cli doctor
```

---

## 7. Render Plan (JSON-driven)

For pipeline integration, you can drive Chronon3D via a JSON render plan:

```bash
chronon3d_cli render-plan \
    --input /work/render-plan.json \
    --assets-root /work/assets \
    --output /work/output/final.mp4
```

Example `render-plan.json`:

```json
{
    "compositions": [
        {
            "name": "MyScene",
            "width": 1920,
            "height": 1080,
            "frame_rate": {"num": 30, "den": 1},
            "duration": 90
        }
    ]
}
```

---

## 8. Project Structure

Here is how a typical Chronon3D project is organized:

```
my-project/
├── CMakeLists.txt
├── main.cpp                  # Your compositions + render entry point
├── assets/
│   ├── fonts/
│   │   └── Inter-Bold.ttf
│   └── images/
│       └── logo.png
└── output/                   # Rendered PNGs and MP4s
```

Key conventions:
- **Fonts**: place `.ttf` files in `assets/fonts/`
- **Images**: place `.png`/`.jpg` in `assets/images/`
- **Output**: rendered files go to `output/`
- **Asset paths**: reference assets relative to the assets root
  (set via `engine.set_assets_root(...)`)

---

## 9. Architecture Overview

```
Composition
  → Scene
  → RenderGraph
  → FrameGraphCompiler
  → CompiledFrameGraph
  → GraphExecutor
  → RenderBackend (Software CPU-first)
  → output (PNG / MP4)
```

The engine is **headless** and **deterministic** — the same input always
produces the same output. The software renderer is the primary backend;
a Vulkan GPU backend is under development (M4 milestone).

---

## 10. Next Steps

- **Quickstart examples**: [`QUICKSTART.md`](QUICKSTART.md) — 10 copy-paste
  text rendering examples
- **Feature reference**: [`FEATURES.md`](FEATURES.md) — complete inventory
  of rendering, text, camera, and SDK features
- **CLI reference**: [`CLI_REFERENCE.md`](CLI_REFERENCE.md) — all subcommands
  and flags
- **SDK integration**: [`SDK_INTEGRATION.md`](SDK_INTEGRATION.md) — C++,
  C ABI, and Go/Rust/Python boundaries
- **Roadmap**: [`ROADMAP.md`](ROADMAP.md) — milestones and future direction
- **Build fast**: [`FAST_BUILD.md`](FAST_BUILD.md) — incremental build tips

---

## License

MIT — see [`LICENSE`](../LICENSE).
