# Focused P2 Vulkan hardware check — 2026-11-02

## Environment and artifact

- Chronon3D source base: `main` at `9fc4b70031edb2c844b05f6fd989339c4b2251c6`.
- Build: existing local `linux-video-test` binary, built with Vulkan enabled;
  no compilation was performed on the GPU server.
- The stripped executable transferred to the server has SHA-256
  `08277438a73dc0aac38840063db87b2401825202b0adcbc1fdebccc1389eb71f`.
- The build directory was a scratch clone with a dirty working tree, including
  the pre-existing `source_node_native_mesh.cpp` edit and a reduced vcpkg
  manifest. This is targeted local evidence, not clean-SHA certification.
- Remote host: Ubuntu 22.04, glibc 2.35; NVIDIA RTX A4000 (16 GiB), driver
  `595.99.02`. `vulkaninfo` with the strict NVIDIA ICD reported `driverName =
  NVIDIA` and `deviceName = NVIDIA RTX A4000`.
- The local binary requires glibc 2.38 / libstdc++ symbols through 3.4.32. It
  ran on the host with a temporary copy of the local glibc 2.39 loader/runtime
  and vcpkg Vulkan loader. The system NVIDIA ICD and driver libraries remained
  on the remote host.

## Results

Both tests were invoked with
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json`:

| Test | Result |
|---|---|
| `Vulkan native occlusion holds a mesh half behind a 2.5D plane in both authoring orders` | 1/1 case, 75/75 assertions PASS |
| `Vulkan disk bokeh kernel matches the CPU kernel within the frozen tolerance` | 1/1 case, 1,169/1,169 assertions PASS; maximum channel delta `1.34576e-07` versus `1e-3` threshold |

The bokeh test verifies the supported circular disk kernel, HDR highlight
weighting, alpha and CPU/Vulkan numeric parity for its small fixture. The
occlusion test exercises the native Vulkan mesh/plane depth and matte sequence
in both authoring orders. GPU telemetry before/after the second test reported
the A4000 at 6,702 MiB used and 0% utilization while idle.

## Local software DoF regression

On the development host, the already-built software suites were run from
`/home/pater/build/chronon3d-p2-build/build/chronon/linux-video-test`; no
compilation was performed for these runs. The scratch clone is based on
`9fc4b70031edb2c844b05f6fd989339c4b2251c6`, with the pre-existing native-mesh
edit and reduced vcpkg manifest/overlay. The two test source files are
byte-identical to this checkout (SHA-256: render-graph fixture
`15cd8194ff28cf10f8628946ebfdff844263759a0181867d7a86e10efdb04840`, camera
kernel fixture `8ba5c98ffbf8d6d45a5b0f02ebb7317b09cd1a6b8e9497b1a5fcd0f04fd34a64`).
The DoF production-source difference between scratch and this checkout is
comment-only.

| Command filter | Result |
|---|---|
| `chronon3d_render_graph_tests --test-case='PR2-RG-DoF:*'` | 16/16 cases, 979/979 assertions PASS |
| `chronon3d_camera_tests --test-case='PerPixelDOF:*'` | 23/23 cases, 65,599/65,599 assertions PASS |

These software checks cover the current synthetic multi-depth/ROI and
near/far CoC behavior, foreground blur spill and focused-edge halo behavior,
alpha/depth provenance, and 2.5D layer-order occlusion. They complement the
strict-native disk-kernel probe but do not establish software/Vulkan output
parity for the same integrated DoF+occlusion fixture; that acceptance gate
remains open.

## Scope

These results close only the focused hardware probes above. The DoF/occlusion
ticket remains **OPEN**: a multi-depth CoC/ROI fixture, foreground blur
bleed/halo and premultiplied-alpha bounds, and an integrated DoF + occlusion
strict-native/software fixture are still required. AO remains explicitly out
of scope per the ticket's recorded product decision.

## Follow-up validation — 2026-11-02

- Motion was built once at `-j14`: 37/37 Ninja steps completed. The focused
  CTest selection (`camera_focus_sequence`, `camera_sequence`, `camera_shake`,
  `authoring`, `camera_bake_roundtrip`) passed 5/5.
- C3D was configured with the full repository vcpkg manifest. The initial
  `-j14` target-set build linked fresh `chronon3d_render_graph_tests` and
  `chronon3d_backend_registry_tests`; the fresh render-graph DoF filter passed
  16/16 cases and 979/979 assertions. It first stopped at
  `test_prepared_render_plan.cpp` because a shared test header unconditionally
  included the unused `chrononplugin/effects/presets/core.hpp`, absent from the
  isolated clone. Removing that unnecessary include/alias allowed an
  incremental four-step `-j14` build to link `chronon3d_c_abi_tests`; the
  focused `resolved camera samples carry DOF and physical optics` test passed
  1/1 case and 17/17 assertions. Plugin-specific runtime tests remain
  conditional on the absent ChrononPlugin header. The complete original
  four-target set was not rerun, and no all-target clean build is claimed. The
  DoF node and unit-test sources were compiled in the initial invocation.
- The camera `PerPixelDOF:*` filter passed 23/23 cases and 65,599/65,599
  assertions on the already-existing camera test executable; that executable
  was not relinked by the failed build, so this result is supplemental rather
  than fresh-binary evidence.
- The two strict-native Vulkan cases were rerun using the freshly linked local
  `chronon3d_backend_registry_tests` from this validation build. After
  `objcopy --strip-debug`, the transferred executable SHA-256 was
  `b08422fe69a057c9fca6bae4d67932feded180cd06165c848f2ce07cac7a66b1`.
  Ubuntu 22.04's system loader cannot run it directly (the executable requires
  GLIBC 2.38 and GLIBCXX 3.4.32); it was launched with the existing temporary
  glibc 2.39 runtime bundle in `/tmp/chronon-p2-gpucheck/runtime`, while Vulkan
  ICD and NVIDIA driver libraries remained from the host. The remote hash
  matched the local stripped executable. With
  `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json`, the device reported
  `NVIDIA RTX A4000`, `driverName = NVIDIA`. Occlusion passed 75/75 assertions;
  disk bokeh parity passed 1,169/1,169 with maximum channel delta
  `1.34576e-07` against `1e-3`. These probes still do not cover the integrated
  DoF-plus-occlusion software/Vulkan acceptance fixture.
