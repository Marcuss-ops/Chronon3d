# Chronon3d Roadmap

This roadmap reflects the current codebase and the next structural steps needed to turn Chronon3d into a scalable motion graphics engine.

> Last updated: 2026-06-05 — verified against codebase

---

## ✅ What Works Now

| Feature | Status |
|---------|--------|
| Modular render graph (DAG + caching) | ✅ complete |
| 2.5D camera — zoom, FOV, pan, tilt, roll, orbit, POI | ✅ complete |
| Camera motion presets | ✅ dolly, pan, orbit, tilt_roll, push_in_tilt, parallax_sweep, dramatic_push, roll_reveal |
| Shapes: Rect, RoundedRect, Circle, Line | ✅ complete |
| Gradient fill (linear + radial) | ✅ on Rect/RoundedRect/Circle |
| Trim path / animated stroke | ✅ `LineParams.stroke.trim_start/end` [0..1] |
| Text (StbFontBackend) | ✅ requires explicit backend injection |
| Image (StbImageBackend) | ✅ requires explicit backend injection |
| Native 3D shapes: FakeBox3D, GridPlane, FakeExtrudedText | ✅ |
| Effects: Blur, Tint, Brightness, Contrast, DropShadow, Glow | ✅ |
| Blend modes | ✅ Normal + others |
| Masks | ✅ in modular graph |
| SSAA | ✅ |
| Motion blur (subframe accumulation) | ✅ |
| Layer parenting (hierarchy, null layer) | ✅ |
| Precompositions | ✅ |
| Video layers (FFmpeg) | ✅ |
| Adjustment layers | ✅ |
| CLI renderer | ✅ |
| Layer rotation 3D (pipeline) | ✅ |
| **Video as 3D card** | ✅ V1: `video_size()` on LayerBuilder, `VideoSource.size` |
| **TextBox + YouTube presets** | ✅ `text_box()`, `youtube::title_card/subtitle_box/lower_third/quote_box()` |
| **Particles overlay** | ✅ `presets::particles()` with configurable circles, streaks, glow |
| **Studio presets** | ✅ `Studio::glass_panel/contact_shadow/fake_box3d/grid_plane/fake_extruded_text`, `Effects::bloom_*` |
| **Motion presets (spring-animated)** | ✅ `motion::title_card()`, `motion::lower_third()`, `motion::progress_bar()` |
Qui non basta muovere la camera. Serve avere un sistema più completo:

camera null object
camera target
parenting pulito tra camera e null
keyframe multipli
curve editor equivalente, anche se solo via codice
ease graph
auto-orient verso target
focus distance collegata a layer
depth of field più controllabile
preset componibili
coordinate system super coerente
export/debug del percorso camera
## 🟡 Important Features (Completed)

| Feature | Status |
|---------|--------|
| **Keyframe curves** | ✅ `KeyframeTrack<T>` with 22 easing types, `AnimatedTransform`, `AnimatedValue<T>` |
| **Spring physics** | ✅ closed-form damped harmonic oscillator: underdamped/overdamped/critical; 4 presets (Gentle, Snappy, Bouncy, Heavy) |
| **Track matte** | ✅ alpha/luma/inverted + 3D source projected correctly |
| **Full 3D parenting** | ✅ resolver gerarchico matrix-correct |
| **Depth of Field** | ✅ V1 per-layer on projected 2.5D layers |
| **Lights (ambient + directional)** | ✅ V1: ambient + directional diffuse Lambert |
| **Shadows layer-to-layer** | ✅ V1: projected 2.5D, directional light only |
| **Parallel multi-frame render** | ✅ Taskflow `tf::Executor` + `tf::Taskflow`: scene eval serial, frame rendering parallel |

## 🟢 Nice To Have (Not Yet Implemented)

| Feature | Note | Effort |
|---------|------|--------|
| Expression system (wiggle, loopOut) | Lua or mini-DSL on `AnimatedValue<T>` | Medium |
| SVG import | Parse path → Chronon3d shapes | Medium |
| Lottie import | Parse JSON → `Composition` | Medium |
| EXR output | ✅ implemented — DWAA tiled + mmap | Low |
| Preview window (SDL2/GLFW + hot reload) | SDL2 in deps, no window code yet | High |
| GPU backend (Vulkan) | Future architecture | Very High |
| WASM / browser | Depends on GPU backend | Very High |
| Python bindings | pybind11 wrapper | High |

## Done Or In Place

- Code-first composition model
- Headless CPU renderer
- Shapes, text, images, layers, and masks
- Blend modes and 2.5D camera support
- Animation helpers: interpolation, easing, spring motion, AnimatedTransform, KeyframeTrack
- CLI for listing, rendering, video export, proof suites
- CMake-only build support
- Tests and CI coverage
- RenderGraph execution engine (pass-splitting, explicit caches, nodes)
- Registry layer (sources, shapes, effects, samplers, blend modes)
- Preset library: motion, studio, particles overlay, text box, YouTube presets

## Block 1: Foundation Cleanup

Goal: keep the repo easy to build and reason about.

- Keep CMake targets and source coverage aligned
- Keep CI paths pointed at `tools/`
- Keep build presets valid on Linux and Windows
- Document architecture and roadmap in-repo
- Add basic benchmark commands for repeatable performance checks

## Block 2: Render Graph (Completed)

Goal: stop growing the renderer as a single monolith.

- [x] Introduce `RenderGraph`
- [x] Split source, mask, effect, transform, composite, and output steps
- [x] Make render passes explicit
- [x] Add graph inspection and debug output
- [x] Add cache keys and dirty tracking per node

## Block 3: Registry Layer (Completed)

Goal: make the engine easier to extend without touching central variants.

- [x] Effect registry
- [x] Source registry
- [x] Blend mode registry
- [x] Sampler registry
- [x] Shape or primitive registry where it makes sense

## Block 4: Production Features

Goal: support real video automation workloads.

- Precompositions
- Video input and frame extraction
- Audio timeline metadata
- Better text layout for subtitles and captions
- Bezier paths, stroke rendering, and trim-path style animation
- Formalized color pipeline and sampling rules

## Block 5: Performance And Tooling

Goal: keep the engine measurable as it grows.

- Node-level and frame-level caching
- Profiling hooks and benchmark suites
- Cache hit and miss reporting
- Render graph diagnostics
- Memory and frame timing summaries in the CLI

## Progression Toward AE

```
Adesso
  ✅ shape types + effects + 2.5D camera + parenting + precomp

+ gradient fills + trim path + rotation test
  → production-ready for basic motion graphics

+ keyframe curves + track matte + DOF + motion blur 3D
  → AE Classic 3D equivalent

+ lights + shadows + spring physics + studio presets + particles
  → professional headless renderer ✅

+ expressions + SVG/Lottie import + EXR
  → AE Classic 3D completed

+ GPU backend + hot reload + Python bindings
  → platform
```

## Not A Goal

- A GUI editor
- A general-purpose 3D DCC replacement
- Browser rendering
- Real-time GPU-first rendering as the default path

The engine should stay focused on deterministic scripted motion graphics.

## Previous Bugs Resolved ✅

| Bug | Fix |
|-----|-----|
| Text mirrored with POI camera | `proj[0][0] = -focal` for POI path |
| FakeBox3D blank in 2.5D pipeline | Identity `projection_matrix` for native-3D layers |
| SourceNode 3D wrong centered shape | Restored `canvas_center * ssaa_scale * shape_local.to_mat4()` |
| `hash_value` float collision (cache stale) | `hash_bytes(&member, sizeof(f32))` direct |
| Masks in modular graph | ✅ fixed |
| TransformNode with opacity | ✅ fixed |
| Precomp not compositing correctly | ✅ fixed |
| EffectInstance std::any vs std::variant | ✅ fixed |
| Test helpers duplicated in 3 files | ✅ extracted to `tests/helpers/` |
| WHOLE_ARCHIVE linking + ODR violation CLI | ✅ fixed |
