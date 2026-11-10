# Composition legacy executor demolition — close-out evidence (2026-11-10)

**Ticket:** [TICKET-COMPOSITION-LEGACY-EXECUTOR-DEMOLITION](../../tickets/TICKET-COMPOSITION-LEGACY-EXECUTOR-DEMOLITION.md)

**Status:** CODE COMPLETE — working tree only, not committed. The ticket is
closed for **demolition**; this report does **not** claim pixel, sequence or
video parity.

## Scope

Remove the legacy `Composition` executor and make `CompiledProgram` the only
production execution input for the CLI, daemon/IPC, SDK, video export and pipe
export. `CompiledComposition` survives only as the evaluation artifact used by
`PreparedRenderPlan` and authoring tests; no production executor consumes it.

## Change

- Deleted the render-graph executor fragments
  (`composition_frame_prepare.cpp`, `composition_frame_single.cpp`,
  `composition_frame_finalize.cpp`, `temporal/composition_frame_temporal.cpp`,
  `composition_frame_internal.hpp`, `pipeline/composition.cpp`).
- Deleted `RenderPipeline` compatibility helpers (`render_compatibility*`,
  `render_compiled_composition`, `StagePool`, `RenderBatchResult`,
  `FrameEncoder`) and `runtime/render_pipeline_batch.cpp`.
- Deleted `PreparedRenderJob` and its implementation fragments,
  `RenderEngine::prepare/render(Composition)`, `render_compiled`,
  `SoftwareRenderer::render(Composition)`, the prepared-composition memo and
  session authoring fields.
- Deleted Composition SDK/CLI/daemon session paths and
  `ipc/composition_session.{hpp,cpp}`. Remaining composition/job IPC commands
  fail closed with an explicit error.
- `RenderJob` now carries only `program`; `resolve_render_request` returns
  `UnsupportedMode` when no `compiled_program` is supplied.
- Pipe export and video export no longer carry a `CompiledComposition` view.
- Tests of removed APIs were retired from the build (`PreparedRenderJob`,
  render request, planning, SDK grid background/assets isolation, motion-blur
  integration). Code-first fixtures use the test-only
  `TestRenderer::render(Composition)` helper, which calls
  `compile_composition` + `evaluate` + `graph::render_scene_via_graph`.

## Verification

| Gate | Command | Result |
|---|---|---|
| Full build | `cmake --build build/chronon/linux-video-release -j12` | exit 0; final pass **64/64**, then `ninja: no work to do` |
| Architecture | `python3.11 tools/check_architecture.py` | **218/218** rules pass |
| Whitespace | `git diff --check` | exit 0 |
| CLI suite | `chronon3d_cli_tests` | **265/265** cases, **5,147/5,147** assertions |
| SDK suite | `chronon3d_sdk_tests` | **3/3** cases, **27/27** assertions |
| Timeline suite | `chronon3d_timeline_tests` | **43/43** cases, **532/532** assertions |
| Scene suite | `chronon3d_scene_tests` | **275/275** cases, **1,607/1,607** assertions |
| Media/video suite | `chronon3d_media_video_tests` | **150/150** cases, **22,222/22,222** assertions |
| Zero-copy suite | `chronon3d_zero_copy_capability_tests` | **49/49** cases, **235/235** assertions |
| Renderer core | `chronon3d_renderer_core_tests` | **211/211** cases, **72,476/72,476** assertions |
| Compiled-program equivalence filter | `chronon3d_core_tests --test-case='*equivalence*'` | **2/2** cases, **29/29** assertions |

## Unrelated red suites

These failures are outside the demolition and were not fixed in this change;
they are recorded so the close-out is not mistaken for a full-suite green.

| Suite | Result | Observed failure |
|---|---|---|
| `chronon3d_camera_tests` | 92/93 cases, 10 failed assertions | camera capability contract / iris-shaping expectations |
| `chronon3d_render_graph_tests` | 457/460 cases, 4 failed assertions | compiler golden hash, GPU word spacing, DoF iris capability |
| `chronon3d_deterministic_tests` | 91/94 cases, 3 failed assertions | missing `chronon3d_cli` discovery, Vulkan validation, native MP4 prerequisite |
| `chronon3d_core_tests` | 1,485/1,487 cases, 4 failed assertions | text unit/grapheme mapping |
| `chronon3d_compositor_tests` | 70/71 cases, 3 failed assertions | composite origin regression |
| `chronon3d_c_abi_tests` | 461/462 cases, 1 failed assertion | legacy fingerprint constant |

## Equivalence gates

**NOT RUN as a legacy-vs-typed comparison.** The demolition removed the legacy
lane, so the previously planned still/sequence/video decoded-pixel comparison
can no longer be executed from this tree. No parity PASS is claimed.
Archived-baseline parity for the typed route remains a separate certification.

## Residual optional consumers

`apps/chronon3d_bench` (`CHRONON3D_BUILD_BENCHMARKS=OFF`) and
`tests/install_consumer` (`CHRONON3D_BUILD_INSTALL_CONSUMER_TEST=OFF`) are not
part of the verified build and still contain authoring examples that call
removed APIs. They are follow-up cleanup, not production executor callers.
