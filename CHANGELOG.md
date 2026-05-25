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
