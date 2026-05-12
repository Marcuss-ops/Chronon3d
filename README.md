# Chronon3d 🚀

Chronon3d is a high-performance, **code-first** C++20 engine designed for procedural video and 3D scene generation. Inspired by modern web animation frameworks like Remotion, but built with the raw power and determinism of C++.

## ✨ Features

- **Code-First Architecture**: Define your videos using C++ lambdas. No more static JSON configs.
- **Fluent Scene DSL**: A clean, type-safe API for building complex 3D scenes.
- **Temporal Primitives**: Built-in `interpolate()` and `Sequence` logic for smooth animations.
- **Determinitic Rendering**: Frame-perfect output, ideal for headless video production.
- **SIMD Optimized**: Built on Google Highway and meshoptimizer for extreme performance.

## 🛠️ Getting Started

### Define a Composition (Remotion-style)

```cpp
#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

Composition MyVideo() {
    return composition({
        .id = "MyAwesomeVideo",
        .width = 1920,
        .height = 1080,
        .fps = 30,
        .duration = 150, // 5 seconds
        .render = [](const FrameContext& ctx) {
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
    });
}
```

### Build and Run

#### Windows
```powershell
.\chronon-win.ps1
.\build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe --composition MyAwesomeVideo --frame 30
```

#### Linux
```bash
./chronon-linux.sh
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

## 🤝 Contributing

Chronon3d is in early development. We prioritize performance, testability, and a clean, modern C++ API.
