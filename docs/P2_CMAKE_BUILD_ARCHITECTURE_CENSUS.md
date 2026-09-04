# P2.12 — CMake/build architecture census

Status: P2 build-graph cleanup census.

## Preset contract

The supported Linux inner-loop/release presets are:

- `linux-fast-dev`
- `linux-video-fast-dev`
- `linux-video-release`

`CMakePresets.json` includes dedicated preset files for the two fast variants,
and `linux-video-release` is defined alongside the video fast/test presets.
Historical preset names must not be reintroduced as aliases: scripts should
consume these canonical names directly.

## Source manifest audit

Run:

```sh
python3 tools/cmake_source_manifest_audit.py
```

The audit reports literal C/C++/CUDA source references in CMake that point to
missing files and sources below `src/` or `apps/` without a literal manifest
reference. Variable/generated manifests are reported conservatively rather
than guessed; a clean configure/build remains authoritative.

Use `--strict-unlisted` only for targeted cleanup because source lists assembled
through generated variables can legitimately appear as census candidates.

## Dependency propagation

Rules:

1. `PUBLIC` means a consumer needs the dependency to compile/link against the
   exported/public surface. It must not be used merely because an OBJECT
   library implementation needs a library.
2. Registry-exposed OBJECT libraries currently reach the pipeline through
   `$<TARGET_OBJECTS:...>`. Their external link requirements need an explicit
   aggregate-boundary decision before blindly converting existing PUBLIC links
   to PRIVATE; otherwise object code can reach the final link without its
   libraries.
3. New implementation-only include paths are PRIVATE.
4. Generated/private backend include directories must not leak into SDK install
   interfaces.

### Current concrete findings

- `src/CMakeLists.txt` already gates the Vulkan subdirectory behind
  `CHRONON3D_ENABLE_VULKAN`.
- Native FFmpeg discovery and the lightweight/full FFmpeg interface targets are
  gated behind `CHRONON3D_ENABLE_NATIVE_FFMPEG`.
- CUDA interop sources are added only inside the Vulkan target when
  `CHRONON3D_ENABLE_CUDA_INTEROP` is enabled.
- The CUDA/Vulkan external-memory probe still carried a historical hard-coded
  `vcpkg_installed/linux-fast-dev/x64-linux/include` path. P2.12 removes that
  path; dependency discovery must come from the toolchain/preset boundary.
- The pipeline currently exposes registered OBJECT files directly. This is the
  reason CUDA/Vulkan/FFmpeg link visibility must be audited together with the
  object aggregation design rather than changed mechanically one target at a
  time.
- `src/CMakeLists.txt` links the JSON-schema validator at the pipeline boundary;
  a second IPC-conditional link of the same validator is redundant and should
  be removed when that file is next touched with build certification available.

## Target-size policy

Do not create a target merely to wrap one implementation file unless it creates
one of these real boundaries:

- optional compilation feature;
- dependency/link isolation;
- ABI/install boundary;
- independent incremental-build unit with measurable benefit;
- code-generation/tool boundary.

Tiny targets that only forward the same includes/libraries to the same aggregate
are merge candidates.

## CUDA / Vulkan / native FFmpeg isolation

Feature-specific compiler/link requirements must stay behind their feature gate:

- Vulkan: `CHRONON3D_ENABLE_VULKAN`
- CUDA interop: `CHRONON3D_ENABLE_CUDA_INTEROP` and Vulkan
- native FFmpeg: `CHRONON3D_ENABLE_NATIVE_FFMPEG`

The lean `linux-fast-dev` preset keeps all three off. The video presets turn on
only the video/GPU stack intentionally.

## Build certification still required

A P2.12 closure run must record, from a clean workspace:

```sh
cmake --preset linux-fast-dev
cmake --build --preset linux-fast-dev
ctest --preset linux-fast-dev-test

cmake --preset linux-video-fast-dev
cmake --build --preset linux-video-fast-dev

cmake --preset linux-video-release
cmake --build --preset linux-video-release
```

For the incremental benchmark, touch one representative software TU, one Vulkan
TU and one video/native-FFmpeg TU and record rebuilt target/TU count plus wall
clock. Compare against a clean build from the same machine and ccache state.

The current ChatGPT execution environment cannot clone the repository or run the
native toolchain, so this document does not claim those builds or timings were
executed. CI/build logs may be used as certification only when they correspond
to the exact commit being audited.
