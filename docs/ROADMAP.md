# Chronon3d Roadmap

This roadmap reflects the current codebase and the next structural steps needed to turn Chronon3d into a scalable motion graphics engine.

## Done Or In Place

- Code-first composition model
- Headless CPU renderer
- Shapes, text, images, layers, and masks
- Blend modes and 2.5D camera support
- Animation helpers such as interpolation, easing, spring motion, and sequences
- CLI for listing, rendering, video export, and proof suites
- CMake-only build support
- Tests and CI coverage
- RenderGraph execution engine (pass-splitting, explicit caches, and nodes)
- Descriptive registry layer (sources, shapes, effects, samplers, and blend modes)

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

## Not A Goal

- A GUI editor
- A general-purpose 3D DCC replacement
- Browser rendering
- Real-time GPU-first rendering as the default path

The engine should stay focused on deterministic scripted motion graphics.
