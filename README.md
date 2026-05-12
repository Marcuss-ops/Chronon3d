# Chronon3d 🚀

Chronon3d is a high-performance, **code-first** C++20 engine designed for procedural video and 3D scene generation. Inspired by modern web animation frameworks like Remotion, but built with the raw power and determinism of C++.

## ✨ Features

- **Code-First Architecture**: Define your videos using C++ lambdas. No more static JSON configs.
- **Fluent Scene DSL**: A clean, type-safe API for building complex 3D scenes.
- **Temporal Primitives**: Built-in `interpolate()` and `Sequence` logic for smooth animations.
- **Deterministic Rendering**: Frame-perfect output, ideal for headless video production.
- **Parallel Pipeline**: Frames render concurrently via Taskflow; output is always emitted in frame order.
- **SIMD Blending**: Semi-transparent draw paths use Google Highway for vectorized alpha compositing. Opaque paths use direct scanline fill.

## 🛠️ Getting Started

### Define a Composition (Remotion-style)

```cpp
#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

Composition MyVideo() {
    return composition(
        CompositionSpec{
            .name = "MyAwesomeVideo",
            .width = 1920,
            .height = 1080,
            .frame_rate = {30, 1},
            .duration = 150 // 5 seconds
        },
        [](const FrameContext& ctx) {
            // 1. Calculate animations based on the current frame
            auto opacity = interpolate(ctx.frame, 0, 30, 0.0f, 1.0f);
            auto pos_y = interpolate(ctx.frame, 30, 90, 0.0f, 200.0f, Easing::InOutQuad);

            // 2. Build the scene fluently
            SceneBuilder builder(ctx.resource);
            
            builder.rect("background", {0, 0, 0}, Color::black());
            
            builder.rect("moving_box", 
                {ctx.width / 2.0f, pos_y, 0}, 
                Color::blue().with_alpha(opacity)
            );

            return builder.build();
        }
    );
}
```

### Build and Run

Per istruzioni dettagliate, consulta **[BUILDING.md](BUILDING.md)**.

#### Windows (xmake)
```powershell
.\tools\dev.ps1
```

#### Windows (CMake)
```powershell
.\chronon-win.ps1
```

## 🏗️ Architecture

Chronon3d is built on a "Pull-based" rendering model:
1. **Composition**: The high-level specification of your video.
2. **FrameContext**: Passed to every frame, containing timing and memory resources (using `std::pmr`).
3. **SceneFunction**: A lambda that evaluates a specific point in time and returns a `Scene`.
4. **Renderer**: Consumes the `Scene` to produce a `Framebuffer`.

## 📚 Modules

- `chronon3d::math`: SIMD-accelerated 3D math (Vec, Mat, Quat).
- `chronon3d::animation`: Temporal interpolation and sequence management.
- `chronon3d::scene`: Flexible scene graph and fluent `SceneBuilder`.
- `chronon3d::rendering`: Multi-backend rendering pipeline (Software, Tracy-instrumented).

## 📐 Coordinate System & Rendering Rules

Chronon3d follows standard 2D graphics conventions for its core pipeline:
- **Origin (0,0)**: Top-Left corner.
- **Axes**: X increases to the right, Y increases downwards.
- **Positioning**: 
  - `rect` and `circle` use **center-based** positioning.
  - `line` uses absolute pixel coordinates.
- **Rasterization**: Primitives follow a `[min, max)` range logic for deterministic pixel coverage.
- **Blending**: Standard alpha blending (`Normal` mode) is applied by default:
  `out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)`
- **Draw Order**: Primitives are rendered in **insertion order** (Painter's Algorithm).

## 🤝 Contributing

Chronon3d is in early development. We prioritize performance, testability, and a clean, modern C++ API.
