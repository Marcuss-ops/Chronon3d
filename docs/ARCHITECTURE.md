# Chronon3d Architecture

Chronon3d is a code-first, headless, CPU-only motion graphics engine. The goal is to provide a maintainable rendering engine for scripted compositions, proof scenes, automated video generation, and repeatable frame rendering.

## Core Principles

- **Code-first**: Composition is the primary authoring model.
- **Deterministic**: Same inputs, same pixels.
- **Headless**: No UI in the core.
- **CPU-only**: Focus on maintainable, SIMD-optimized software rendering.

For detailed guidelines on contributing and the project mantra, see [GOVERNANCE.md](docs/GOVERNANCE.md).

> **core only builds, adapters only call, data only flows in.**
>
> *Core defines the domain. Runtime orchestrates rendering.*
> *Backends rasterize, decode, encode, and perform concrete IO.*
> *Operations expose thin adapters. CLI parses human intent only.*
> *Tools and examples must never become architecture.*

## Project Model

### Core Rule

- Put **behavior** in code.
- Put **variation** in data.
- Put **orchestration** in the CLI.
- Put **build selection** in CMake presets.
- Prefer shared utilities that any feature can consume, instead of one-off logic owned by a single clip.

### What Belongs Where

| Concern | Medium | Example |
|---------|--------|---------|
| Rendering behavior, camera motion math, composition evaluation | Code | `src/backends/software/`, `src/runtime/` |
| Camera axis, duration, frame range, render profiles, codec options | Data | Params structs, CLI arguments |
| `doctor`, `verify`, `render`, `video` | CLI | `apps/chronon3d_cli/` |
| Camera motion reusable across image, text, video | Shared utilities | `include/chronon3d/presets/` |
| `win-debug`, `win-release`, etc. | Build presets | `CMakePresets.json` |

### Rule Of Thumb

If you are changing:
- a **value**, use data
- **behavior**, use code
- **user intent**, use the CLI
- **build environment**, use presets

## Topology

- `include/chronon3d/` — Public API.
- `src/runtime/` — Pipeline and evaluation.
- `src/render_graph/` — Execution model.
- `src/backends/software/` — Main software rasterizer.
- `apps/chronon3d_cli/` — Human entry point.

## Runtime Flow

1. **Evaluation**: `Composition` resolves time to `Scene`.
2. **Graph Building**: `Scene` converts to `RenderGraph`.
3. **Execution**: `GraphExecutor` processes nodes, hitting `NodeCache`.
4. **Compositing**: `SoftwareCompositor` blends layers into a `Framebuffer`.

## Internal Layers

### Public API
The public headers define the surface area for animation, math, scene building, and rendering primitives.

### Scene Model
Captures what should be drawn, in what order, with which transforms, masks, and effects.

### Evaluation
Resolves code-first composition logic into frame-specific runtime data (animations, timelines).

### Rendering
Software-based rasterization handling shapes, images, text, masks, 2.5D projection, and effects.

## Render Graph (modular)

The render graph (`chronon3d::graph`) is the execution model for a single frame. Each operation — source rendering, masking, effects, compositing — is a `RenderGraphNode` with explicit inputs and outputs.

### Key Characteristics:
- **Coordinate Systems**: Uses top-left (screen) coordinates for 2D layers and centered coordinates for 3D/Projected layers.
- **Dependency Caching**: `GraphExecutor` combines node hashes with input digests to ensure correct invalidation.
- **Adjustment Layers**: Apply effects to previous results without a source.

## Data Flow Boundaries

Chronon3d keeps these responsibilities separate:
- Composition code decides intent.
- Evaluation resolves time-dependent values.
- Scene objects describe the frame.
- Rendering performs pixel work.
- IO writes the final frame.

## Testing Philosophy

See [Testing Philosophy](docs/GOVERNANCE.md#4-testing-philosophy) in the Governance document.

## Architectural Decisions

### 1. ABI Stability: Separate Params from Render State

**Problem:** Structs like `FakeBox3DShape` mixed public data (user-configured) with private data (injected at render time).

**Solution:** Completely separate the two domains:

```cpp
// Public data (Stable API, serializable)
struct FakeBox3DParams {
    Vec3  pos;
    Vec2  size;
    f32   depth;
    Color color;
};

// Runtime data (produced by render graph builder, exists only inside renderer)
struct FakeBox3DRenderState {
    Mat4  cam_view;
    f32   focal, vp_cx, vp_cy;
    bool  cam_ready;
};
```

Prebuilt libraries only see `FakeBox3DParams`, which changes rarely.

### 2. Explicit File Lists for Critical Targets

**Problem:** Glob wildcards (`add_files("*.cpp")`) collect everything. A stray file breaks the build with duplicates.

**Solution:** Explicit files for CLI and core targets; globs only for additive directories.

```cmake
# CLI: explicit to avoid residues
add_executable(chronon3d_cli
    apps/chronon3d_cli/main.cpp
    apps/chronon3d_cli/commands/*.cpp
)
```

### 3. Self-Registering Compositions

**Problem:** Manual composition index drifts from the file that defines the composition.

**Solution:** Compositions register themselves with `CHRONON_REGISTER_COMPOSITION(...)`. The `CompositionRegistry` loads them automatically.

### 4. Stable Facade Headers

**Problem:** Massive refactoring breaks active sessions.

**Solution:** Stabilize facade headers that never change paths:

- `include/chronon3d/chronon3d.hpp` — umbrella header (stable)
- `include/chronon3d/scene/` — public API (stable)
- `src/` — implementation detail (free to reorganize)

`include/` moves only with a major version bump. `src/` can be reorganized freely.

### 5. Math Constants

**Problem:** `M_PI` is not portable to MSVC.

**Solution:** Centralized header:

```cpp
// include/chronon3d/math/constants.hpp
#pragma once
#include <glm/gtc/constants.hpp>

namespace chronon3d::math {
    inline constexpr float pi      = glm::pi<float>();
    inline constexpr float two_pi  = glm::two_pi<float>();
    inline constexpr float half_pi = glm::half_pi<float>();
    inline constexpr float e       = glm::e<float>();
}
```

Never use `<cmath>` directly in renderer code — always use `math::pi`.

### 6. CLI Utility Decoupling (Core Purity)

**Problem:** Development tools like `WatchService` (filesystem polling) and `BatchRunner` (system-level batching) were previously in `src/runtime/`, mixing development-only intent with the core engine.

**Solution:** Relocated development utilities to `apps/chronon3d_cli/utils/` under the `chronon3d::cli` namespace. The `chronon3d_runtime` remains a pure orchestration layer for composition evaluation and rendering, free from filesystem polling or CLI-specific batch logic.

## Near-Term Engine Direction

- node and asset registries
- precomposition support
- video source handling
- audio timeline metadata
