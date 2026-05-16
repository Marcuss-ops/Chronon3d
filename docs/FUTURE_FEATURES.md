# Future Features

This document lists planned and possible future implementations for Chronon3d, ordered by priority and impact.

---

## Priority 1 — Unblock real production use

### Layer rotation in 3D
**Status:** not implemented  
**Impact:** critical — without this, individual cards cannot be tilted in 3D space  
**What it is:** `l.rotate({x, y, z})` applied to a 3D layer produces a correctly perspective-distorted card  
**Implementation:** force matrix path in `project_layer_2_5d` when layer has non-zero rotation; strip rotation from SourceNode state matrix; `proj * view * (T * R * S)` in TransformNode

### Keyframe curves
**Status:** not implemented — animations are written manually as `f(ctx.frame)`  
**Impact:** critical — without this, all animations require math formulas in C++  
**What it is:** attach keyframes to any property (position.x, opacity, rotation.y) with easing (linear, ease-in, ease-out, bezier)  
**Implementation:** `AnimatedValue<T>` already exists; needs a builder API and integration with the scene evaluation loop

### Gradient fill
**Status:** not implemented  
**Impact:** high — 50% of motion graphics use linear or radial gradients  
**What it is:** `l.rect("bg", {.fill = gradient(Color::black(), Color::blue())})` and radial variant  
**Implementation:** new `GradientShape` or fill parameter on existing shapes; Blend2D already supports gradients natively

### Trim path / animated stroke
**Status:** not implemented  
**Impact:** high — "line draws itself" is the most common motion graphics effect  
**What it is:** stroke with `start` and `end` percentage, animatable; dash offset animatable  
**Implementation:** new `StrokeParams` with `trim_start`, `trim_end`, `dash_offset`; Blend2D stroke API

---

## Priority 2 — Rendering quality

### Depth of Field
**Status:** struct present (`DepthOfFieldSettings`), renderer not implemented  
**Impact:** medium — cinematic look for 3D scenes  
**What it is:** layers farther from `focus_z` receive gaussian blur proportional to Z distance  
**Implementation:** post-process pass after compositing; blur radius = `|layer_z - focus_z| * aperture`

### Motion blur (subframe accumulation)
**Status:** struct present (`MotionBlurSettings`), 3D path not integrated  
**Impact:** medium — smooth movement on fast-moving layers  
**What it is:** render N subframes per frame with fractional `frame_time`, accumulate with alpha  
**Implementation:** `SoftwareRenderer` already has `frame_time` in context; needs integration with 3D projection path

### Lights — ambient and directional
**Status:** `LightContext` and `Material2_5D` structs present, no pipeline  
**Impact:** medium — needed for 3D objects that receive shading  
**What it is:** scene-level lights that affect layers with `material.accepts_lights = true`  
**Implementation:** pass `LightContext` through `RenderState`; apply N·L shading in shape processors

### Lights — point and spot
**Status:** not designed  
**Impact:** low-medium — more realistic lighting for close-up 3D objects  
**Depends on:** directional lights first

### Shadows — layer to layer
**Status:** not implemented  
**Impact:** medium — layers casting shadows onto layers below them in Z  
**What it is:** shadow map pass per light source; composite shadow before layer blending  
**Depends on:** lights system

---

## Priority 3 — Content types

### Particle system
**Status:** not implemented  
**Impact:** high — backgrounds, transitions, dust, sparks, logo reveals  
**What it is:** emitter with count, lifetime, velocity, spread, color over life, size over life  
**Implementation:** `ParticleEmitter` evaluated per frame; each particle is a small shape or point

### Video layer as 3D card
**Status:** FFmpeg pipeline exists; 3D card path not connected  
**Impact:** high — video footage placed in 3D space with perspective  
**What it is:** `l.image("clip", {.path = "footage.mp4", .frame = ctx.frame})` in 3D  
**Implementation:** connect `VideoSource` to the projected card rasterizer path

### SVG import
**Status:** not implemented  
**Impact:** medium — import vector assets directly  
**What it is:** render SVG paths as Chronon3d shapes with correct transforms  
**Implementation:** parse SVG path data; emit `LineParams` / `RectParams` / `CircleParams`

### Lottie import
**Status:** not implemented  
**Impact:** medium-high — large ecosystem of existing animations  
**What it is:** parse Lottie JSON; evaluate per frame into Chronon3d scene  
**Implementation:** Lottie parser → `Composition` factory

---

## Priority 4 — Animation system

### Expression system
**Status:** not implemented  
**Impact:** high — `wiggle`, `loopOut`, `time * 360`, `Math.sin(time)` on any property  
**What it is:** small scripting layer (Lua or a custom DSL) attached to animated properties  
**Implementation:** `ExpressionValue<T>` wrapping an `AnimatedValue<T>`; evaluate at frame time

### Spring physics
**Status:** `SpringAnimation` struct exists  
**Impact:** medium — natural-feeling motion without manual easing  
**What it is:** property follows a spring toward a target; settles naturally  
**Implementation:** evaluate spring ODE per frame; integrate with keyframe system

### Parenting — full 3D hierarchy
**Status:** 2D parenting works; 3D parenting only propagates translation  
**Impact:** medium — rig a camera dolly that moves a group of 3D layers  
**What it is:** child layer inherits parent's full 3D transform (position + rotation + scale)  
**Implementation:** compute `world_transform = parent_world * local_TRS` recursively

### Track matte
**Status:** not implemented  
**Impact:** medium — use one layer as alpha or luma matte for another  
**What it is:** layer A's alpha controls layer B's visibility  
**Implementation:** new graph node `TrackMatteNode`; sample A's alpha during B's compositing

---

## Priority 5 — Output and tooling

### Parallel multi-frame rendering
**Status:** serial; Taskflow already linked  
**Impact:** high — render 90 frames in 3s instead of 30s on 8-core  
**What it is:** render each frame on a separate thread; write PNGs in parallel  
**Implementation:** `tf::Taskflow` with one task per frame; thread-safe `NodeCache`

### EXR output
**Status:** not implemented  
**Impact:** medium — 32-bit float per channel for professional compositing  
**What it is:** save framebuffer as OpenEXR for use in Nuke/Resolve/Blender  
**Implementation:** link `openexr` via vcpkg; add `save_exr()` alongside `save_png()`

### Interactive preview window
**Status:** not implemented — engine is headless  
**Impact:** high — see result immediately without CLI round-trip  
**What it is:** window that renders the current frame and updates on recompile  
**Implementation:** SDL2 or GLFW window; texture upload from `Framebuffer`; hot reload trigger

### Hot reload compositions
**Status:** not implemented  
**Impact:** high — edit a composition, see it update without relaunching CLI  
**What it is:** watch `.cpp` file for changes; recompile only that TU; `dlopen` new shared lib  
**Implementation:** `inotify` watcher + `clang` incremental compile + `dlopen`

### SpecScene format — full scene description
**Status:** partial (TOML parser exists, limited fields)  
**Impact:** medium — author scenes in a text format without recompiling  
**What it is:** complete mapping of all scene properties to TOML/JSON  
**Implementation:** extend `specscene` parser to cover all `LayerBuilder` and `SceneBuilder` APIs

---

## Priority 6 — Architecture

### GPU backend (Vulkan / Metal / WebGPU)
**Status:** not designed — all rendering is CPU  
**Impact:** high long-term — 10-100x faster rendering for complex scenes  
**What it is:** same public API, new backend that rasterizes on GPU  
**Implementation:** abstract `Rasterizer` interface; Vulkan implementation behind feature flag  
**Depends on:** CPU backend stable and API frozen

### WASM / browser target
**Status:** not designed  
**Impact:** medium — run Chronon3d in the browser for web-based tools  
**What it is:** compile to WebAssembly; output canvas pixels via JS interop  
**Depends on:** GPU backend or emscripten software path

### Python bindings
**Status:** not implemented  
**Impact:** medium — scripting animations from Python without recompiling  
**What it is:** `pybind11` wrapper around `SceneBuilder`, `Composition`, `SoftwareRenderer`  
**Implementation:** thin binding layer; no new engine logic

---

## Known bugs to fix first

Before any new feature, these must be resolved:

1. `EffectInstance` uses `std::any` but tests expect `std::variant` — align them
2. Masks do not apply correctly in the modular graph path
3. `TransformNode` with opacity produces wrong pixel value
4. Precomposition color not composited correctly
5. Text appears mirrored with POI camera
6. `FakeBox3D` renders blank in 2.5D pipeline
7. Image layer has hardcoded `{-1000,-1000,1000,1000}` bbox (TODO in code)
8. Test helpers duplicated across 3 files — extract to `tests/helpers/`

---

## What "complete" looks like

```
Layer rotation in 3D  +  keyframe curves  +  gradients  =  usable for real production
+ lights + shadows + DOF + motion blur    =  AE Classic 3D equivalent
+ GPU backend + hot reload + expressions  =  professional headless renderer
```
