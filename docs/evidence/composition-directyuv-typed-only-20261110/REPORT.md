# Composition legacy executor — DirectYUV typed-only tranche (2026-11-10)

**Ticket:** [TICKET-COMPOSITION-LEGACY-EXECUTOR-DEMOLITION](../../tickets/TICKET-COMPOSITION-LEGACY-EXECUTOR-DEMOLITION.md)

**Status:** CODE COMPLETE — working tree only, not committed; the parent ticket remains **ACTIVE**.

**Scope:** Remove the `CompiledComposition` DirectYUV overloads and make the
legacy lane fail closed. This does not migrate the SDK, registry, RenderEngine,
renderer or daemon compatibility consumers, and it does not prove
still/sequence/video equivalence.

## Change

- Removed `DirectYuvProgram::resolve_support(const CompiledComposition&, ...)`.
- Removed `DirectYuvProgram::prepare(const CompiledComposition&, ...)` with
  both `LaneReason` and the unused `std::string` reason overloads.
- `DirectYuvProgram::prepare_impl` no longer accepts a `CompiledComposition*`;
  the `composition_` member and the `execute()` fallback that called
  `composition_->evaluate()` are removed.
- `finalize_composition_execution()` no longer accepts a `CompiledComposition`.
  If the resolver selected DirectYUV but no typed program exists, `Auto`
  downgrades to FullGraph with reason `DirectYUV requires a typed
  CompiledProgram`; `RequireDirectYuv` fails with
  `direct_yuv_capability_required`.
- `pipe_export_pipeline_setup` fails closed if a DirectYUV plan reaches setup
  without a typed program. It never calls a legacy overload.
- DirectYUV execution now requires the typed program and typed topology; a
  missing topology is a frame failure, never a legacy evaluation fallback.

## Verification

| Gate | Command | Result |
|---|---|---|
| CLI build | `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j12` | exit 0 |
| Architecture | `python3.11 tools/check_architecture.py` | **218/218** rules pass; caps `compile_composition` 15, compatibility render 6, legacy frame executor 9 |
| Media/video suite | `chronon3d_media_video_tests` | **150/150** cases, **22,222/22,222** assertions |
| CLI suite | `chronon3d_cli_tests` | **293/293** cases, **5,688/5,688** assertions |
| Zero-copy suite | `chronon3d_zero_copy_capability_tests` | **49/49** cases, **235/235** assertions |
| DirectYUV text contract | `chronon3d_zero_copy_capability_tests --test-case='DirectYUV text contract*'` | **10/10** cases, **37/37** assertions |
| Whitespace | `git diff --check` | exit 0 |

The tests exercise the typed DirectYUV contract and the CLI export boundary.
The compile step is also the regression gate for the deleted API: any remaining
caller of the legacy overloads would fail to build.

## What remains

- `RenderEngine::prepare(const Composition&)`, `PreparedRenderJob`
  compatibility rendering, `render_compatibility*`,
  `render_compiled_composition_frame*`, software prepared-composition memo and
  CLI/SDK/daemon compatibility request fields are still live.
- The pinned production caller counts remain 15 / 6 / 9; this tranche retires
  one item in the removal scope, it does not reduce those counts.
- Still, sequence and video equivalence, camera/shutter parity, cancellation
  and reverse-order determinism, DirectYUV/GOP parity, cache-version rejection
  and the 8-case C22 corpus are **NOT RUN**.
- The parent ticket remains **ACTIVE**. Do not close it on this tranche alone.

## Files changed

- `include/chronon3d/media/video/direct_yuv_program.hpp`
- `src/media/video/direct_yuv/direct_yuv_program_prepare.cpp`
- `src/media/video/direct_yuv/direct_yuv_program_execute.cpp`
- `src/media/video/direct_yuv/direct_yuv_program_stub.cpp`
- `src/media/video/direct_yuv/direct_yuv_capability.cpp`
- `apps/chronon3d_cli/utils/video/video_job_execute_dispatch.cpp`
- `apps/chronon3d_cli/commands/video/exporters/pipe_export_pipeline_setup.cpp`
- `docs/tickets/TICKET-COMPOSITION-LEGACY-EXECUTOR-DEMOLITION.md`
- `docs/tickets/TICKET-CHRONON-2.0-MASTER.md`
- `docs/CURRENT_STATUS.md`
- `docs/FOLLOWUP_TICKETS.md`
