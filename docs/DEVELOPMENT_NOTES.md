# Development Notes

This document contains notes on past issues, architectural decisions, and best practices for Chronon3d.

---

## 1. ABI Instability between Structs and Prebuilt Libraries

**Root Cause:** Structs like `FakeBox3DShape`, `GridPlaneShape`, etc., mixed "public" data (user-configured) and "private" data (injected at render time). Anyone modifying the public part broke the alignment of the private part.

**Solution:** Completely separate the two domains.

```cpp
// Public data (Stable API, serializable, structure never changes)
struct FakeBox3DParams {
    Vec3  pos;
    Vec2  size;
    f32   depth;
    Color color;
};

// Runtime data (NEVER shared with examples or plugins)
// Produced by the render graph builder, exists only inside the renderer
struct FakeBox3DRenderState {
    Mat4  cam_view;
    f32   focal, vp_cx, vp_cy;
    bool  cam_ready;
};
```

The renderer receives `FakeBox3DParams` and `FakeBox3DRenderState` separately — the public Shape never has runtime fields. Prebuilt libraries only see `FakeBox3DParams`, which changes rarely.

---

## 2. Glob Wildcards in the Build System + Legacy Residual Files

**Root Cause:** `add_files("*.cpp")` collects everything it finds. A file left by mistake breaks the build with duplicates.

**Solution:** Explicit files for critical targets, globs only for examples directories (which are additive, never subtractive).

```lua
-- CLI: explicit to avoid residues
target("chronon3d_cli")
    add_files(
        "apps/chronon3d_cli/main.cpp",
        "apps/chronon3d_cli/commands/*.cpp",
        "apps/chronon3d_cli/utils/*.cpp"
    )

-- Examples: glob is fine (only additive, no symbols shared with core)
target("chronon3d_examples_lib")
    add_files("examples/**.cpp")
```

Alternatively: add a CI check that compiles with `-Werror` and verifies zero `LNK2005`.

---

## 3. Compositions Not Visible Without a Central Registry

**Root Cause:** Keeping a manual index separate from the file that defines the composition creates drift and requires a second step every time an example is added.

**Solution:** Compositions register themselves in their translation unit with `CHRONON_REGISTER_COMPOSITION(...)` and the `CompositionRegistry` loads them automatically when constructed. No hidden flags, no manual lists, no manual exceptions: the registry populates itself when compositions are linked.

---

## 4. Massive Refactoring Breaking Active Sessions

**Root Cause:** There is no explicit contract between "Public API" (used by examples, tests, CLI) and "Implementation Detail" (freely reorganizable).

**Solution:** Stabilize a set of facade headers that never change paths, even if internal code is reorganized.

- `include/chronon3d/chronon3d.hpp`      ← umbrella header (stable)
- `include/chronon3d/scene/`             ← public API (stable)
- `include/chronon3d/renderer/`          ← public API (stable)
- `src/renderer/internal/`               ← implementation detail (free to change)

**Rules:**
- `include/` moves only with a major version bump.
- `src/` can be reorganized freely.
- Moving files in `src/` does NOT break anything if headers in `include/` remain stable.

In CMake, this is achieved with `target_include_directories(... PUBLIC include)` and never exposing `src/` externally.

---

## 5. M_PI and Platform-Specific Math Constants

**Root Cause:** Dependency on non-standard POSIX extensions in MSVC.

**Solution:** A centralized header, once and for all.

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

`<cmath>` is never used directly in the renderer code — always use `math::pi`. CI builds on MSVC + GCC + Clang create the safety net.

---

## Implementation Priorities

| # | Impact | Effort | When |
|---|---|---|---|
| Separate params/render-state in 3D structs | Crash → zero | 2h | Immediate |
| `math/constants.hpp` header | Minor rebuild fail | 30min | Immediate |
| Explicit files in CLI target | Build residues | 30min | Immediate |
| Stable facade headers in `include/` | Safe refactoring | 4h | Next sprint |
| Plugin discovery / no `WHOLEARCHIVE` | Architectural debt | 1-2 days | When examples grow |

The first three are small changes with immediate returns. The last one is what eliminates the "incompatible prebuilt library" class of problems once and for all.
