# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Fixed
- **Scene**: Made hierarchy bake idempotent to avoid double-resolving transforms.
- **Scene**: Preserved parent links for validation in complex hierarchies.
- **Render**: Fixed 2.5D passive camera depth sign and parallax calculations.
- **Render**: Aligned modular graph projection with direct software renderer projection.
- **Tests**: Fixed heap-buffer-overflow in adjustment layer tests (ASAN).
- **Tests**: Aligned 2.5D test expectations with new coordinate convention.

### Added
- **License**: Added MIT LICENSE file.
- **Contributing**: Added CONTRIBUTING.md with development guidelines.
- **Static analysis**: Added .clang-tidy config and clang-tidy CI job.
- **N11**: Fixed ShapeType::Path double registration in builtin_processors.cpp.
- **I6**: Enabled DiskNodeCache for static/frame-invariant nodes (disk_cacheable=true).
- **S5**: Added LruCache-backed PathFlattenCache — path contours cached across frames.
- **S8**: Added RenderCountersRaw + merge_tls() for thread-local counter accumulation.
- **M9**: Added CancellationToken for graceful SIGINT/SIGTERM shutdown in video export.
- **M10**: Added --dry-run flag to `video` command — validates composition without rendering.
- **N14**: Added hot-path benchmarks for blur (r10/50/100), compositing blend, and motion blur accumulation.

### Performance

- **CompositeNode**: Skip-When-Opaque early exit — when the top layer is a full-frame opaque source, skip the blend+copy entirely. Cold-start `ImgGridTest` drops from 425ms → 303ms (-29%).
- **GraphExecutor**: Execution plan cache — topological sort + consumer counts are cached across frames when the graph structure is identical. `compute_structure_signature()` hashes node kinds + connectivity to detect changes.
- **SceneHasher**: Static fingerprint fast-path — frame-independent `compute_static_fingerprint()` lets `render_scene_via_graph()` skip graph building + execution entirely for static compositions. `ImgGridTest` goes from 311ms → 0.42ms (740×). `DarkGridBackground` from 3.88ms → 0.34ms (11×).
- **Execution plan + static fingerprint integration**: `RenderGraphContext.graph_structure_unchanged` hint lets `GraphExecutor::execute()` skip `compute_structure_signature()` when the scene structure is unchanged — saving O(n) node iteration per frame for large graphs.
