# Governance — Contributing, Standards & Process

## Core Mantra

> **core only builds, adapters only call, data only flows in.**
>
> *Il Core definisce le leggi. Il Runtime decide il flusso.*
> *I Backend fanno il lavoro concreto. Le Operations chiamano soltanto.*
> *La CLI traduce comandi umani. Tools ed Examples non comandano mai.*

---

## 1. Contribution Guidelines

### Guidelines

1. **Keep responsibilities clean**: Don't leak CLI logic into the renderer. Don't leak renderer logic into the scene model.
2. **Deterministic by design**: Any change to the rendering pipeline must be verified for determinism.
3. **Headless first**: The core engine must never depend on windowing or GUI libraries.
4. **Minimal Tests**: Follow the [Testing Philosophy](#4-testing-philosophy) below.

### Build Model

CMake + vcpkg is the primary build path. All dependencies are managed via `vcpkg.json`.

```bash
export VCPKG_ROOT=~/vcpkg
cmake --preset linux-release
cmake --build build/chronon/linux-release -j$(nproc)
```

---

## 2. Coding Standards & Architectural Guardrails

### 2.1 Dependency Isolation (Core vs. Video)

The core engine (`chronon3d_core`, `scene`, `runtime`) must remain independent of video processing and FFmpeg.

- **Rule**: Never include `<chronon3d/backends/video/video_source.hpp>` in core headers like `layer.hpp`.
- **Reason**: Including video headers in the layer model forces every component (including tests and basic software rendering) to depend on the video backend, dragging in FFmpeg and increasing build times by 50x.
- **Action**: Use forward declarations or move video-specific fields to separate headers (e.g., `layer_video.hpp`) or `.cpp` files.

### 2.2 Test Isolation

Tests that verify core logic (math, animation, basic rendering) should not compile or link against video backends.

- **Rule**: `chronon3d_tests` must only link against `chronon3d_core` and `chronon3d_software`.
- **Action**: If a test requires video functionality, place it in a separate target like `chronon3d_video_tests`.

### 2.3 Build Hygiene

Avoid configuration drift within the same build directory.

- **Rule**: Use separate build directories for different configurations (e.g., video enabled vs. disabled).
- **Reason**: Toggling major features like `CHRONON3D_ENABLE_VIDEO` within the same directory leads to mixed artifacts and inconsistent include paths.
- **Action**: Use CMake presets to manage different build trees (e.g., `build/win-debug` vs. `build/win-debug-video`).

### 2.4 vcpkg Feature Management

Manage third-party dependencies strictly through `vcpkg.json` features.

- **Rule**: Always set `"default-features": []` in `vcpkg.json`.
- **Reason**: This ensures that `ffmpeg` and other heavy dependencies are only installed when explicitly requested by a build feature, preventing accidental dependency bloat in CI and local builds.

### 2.5 Architectural Violations Checklist

If you notice any of the following, fix them immediately:
- [ ] `grep -r "video_source" include/chronon3d/scene/` returns matches in `layer.hpp`.
- [ ] `chronon3d_tests` requires `FFmpeg` to be installed even when video is disabled.
- [ ] Changing a CMake option requires a full re-install of vcpkg dependencies.

---

## 3. Pre-Merge Validation Checklist

Protocol to be executed before every merge or release.

### 3.1 Full Build

Configure and build the project using CMake presets:

```bash
# On Linux
cmake --preset linux-release
cmake --build --preset linux

# On Windows
cmake --preset win-release
cmake --build --preset win
```

**Expected Outcome:**
- No compilation errors
- No new blocking warnings
- Binaries generated correctly (`chronon3d_cli` and test suites)
- No linker errors

### 3.2 Automated Tests

Run the complete test suite using CTest presets:

```bash
# On Linux
ctest --preset linux-test --output-on-failure

# On Windows
ctest --preset win-test --output-on-failure
```

**Expected Outcome:** All tests pass, no crashes.

### 3.3 Render Single Frame

Verify CLI rendering by running a sample composition render:

```bash
# On Linux
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render MyComp --frame 0 -o output/test_render.png

# On Windows
.\build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe render MyComp --frame 0 -o output/test_render.png
```

**Expected Outcome:** PNG created, no visual artifacts.

### 3.4 Clean Git Status

```bash
git status --short
```

**Expected Outcome:** No unwanted files, only the changes intended to be kept.

### 3.5 Check for Conceptual Duplications

```bash
rg -n "templates|visual_presets|rendergraph|examples/proofs" include src tests docs
```

**Expected Outcome:** No old layers re-emerging, no logic duplicated under new names.

### Summary

| # | Check | Outcome |
|---|-------|---------|
| 1 | Build passes | ✅ / ❌ |
| 2 | Tests pass | ✅ / ❌ |
| 3 | CLI render correct | ✅ / ❌ |
| 4 | `git status` clean | ✅ / ❌ |
| 5 | No duplicates (`rg`) | ✅ / ❌ |

All ✅ → ready for release.

---

## 4. Testing Philosophy

The `tests/` directory must remain as minimal as possible.

Tests exist to verify the behavior of the engine, not to recreate engine logic, duplicate runtime flows, or become an alternative implementation of the system.

Every extra line written inside a test is a signal that something may be missing, misplaced, or poorly exposed in the real codebase. If a test needs too much setup, too much scene-building logic, or too much repeated boilerplate, the first assumption should be that the production API, a helper, a fixture, or a dedicated utility is missing.

A long test is usually a sign of weak boundaries.

The responsibility of the test is only to express:

1. the scenario being tested
2. the action being performed
3. the expected result

The test should not contain reusable engine logic.

### Test Minimalism Rule

Tests must be minimal. Every unnecessary line inside a test is a sign that the real code may be missing an API, helper, fixture, preset, or properly placed responsibility.

A test should not rebuild the engine from scratch. It should only describe the scenario, execute the behavior, and verify the result.

If the same setup appears in multiple tests, extract it.
If the setup represents real engine behavior, move it into production code.
If the setup exists only for testing, move it into `tests/helpers`.

Long tests are not a badge of completeness. Long tests are usually a symptom of poor boundaries.

The shorter and clearer the test, the healthier the architecture.
