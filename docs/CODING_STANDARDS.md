# Coding Standards & Architectural Guardrails

To maintain high build performance and clean dependency boundaries, all developers and AI agents must adhere to the following rules.

## 1. Dependency Isolation (Core vs. Video)

The core engine (`chronon3d_core`, `scene`, `runtime`) must remain independent of video processing and FFmpeg.

- **Rule**: Never include `<chronon3d/backends/video/video_source.hpp>` in core headers like `layer.hpp`.
- **Reason**: Including video headers in the layer model forces every component (including tests and basic software rendering) to depend on the video backend, dragging in FFmpeg and increasing build times by 50x.
- **Action**: Use forward declarations or move video-specific fields to separate headers (e.g., `layer_video.hpp`) or `.cpp` files.

## 2. Test Isolation

Tests that verify core logic (math, animation, basic rendering) should not compile or link against video backends.

- **Rule**: `chronon3d_tests` must only link against `chronon3d_core` and `chronon3d_software`.
- **Action**: If a test requires video functionality, place it in a separate target like `chronon3d_video_tests`.

## 3. Build Hygiene

Avoid configuration drift within the same build directory.

- **Rule**: Use separate build directories for different configurations (e.g., video enabled vs. disabled).
- **Reason**: Toggling major features like `CHRONON3D_ENABLE_VIDEO` within the same directory leads to mixed artifacts and inconsistent include paths.
- **Action**: Use CMake presets to manage different build trees (e.g., `build/win-debug` vs. `build/win-debug-video`).

## 4. vcpkg Feature Management

Manage third-party dependencies strictly through `vcpkg.json` features.

- **Rule**: Always set `"default-features": []` in `vcpkg.json`.
- **Reason**: This ensures that `ffmpeg` and other heavy dependencies are only installed when explicitly requested by a build feature, preventing accidental dependency bloat in CI and local builds.

## 5. Architectural Violations Checklist

If you notice any of the following, fix them immediately:
- [ ] `grep -r "video_source" include/chronon3d/scene/` returns matches in `layer.hpp`.
- [ ] `chronon3d_tests` requires `FFmpeg` to be installed even when video is disabled.
- [ ] Changing a CMake option requires a full re-install of vcpkg dependencies.

---

*These rules are designed to keep the "test PNG" path as fast as possible: 2 seconds build, zero FFmpeg.*
