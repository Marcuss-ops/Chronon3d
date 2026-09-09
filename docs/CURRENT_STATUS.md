# Chronon3D — Current Status

> **Single v0.1 release contract:** [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md). This file reports observed state; it does not redefine release identity.

## Main truth checkpoint — 2026-09-04

## Migration closeout checkpoint — 2026-09-08

- Wave 4 compiler caller census is CLOSED: no independent production caller
  remains for `build_graph`, `compile_frame_graph`, or `compile_render_plan`;
  the compiler root owns the route.
- The native FFmpeg contract is fail-closed when native libav support is not
  compiled: an explicit `encoder_backend=native` request no longer falls back
  to the external pipe authority. Remaining pipe demolition is certification
  work (supported-consumer, codec, zero-caller, and CI evidence).
- The video-enabled `linux-fast-dev` CLI test target builds cleanly with
  `-j20` (380/380) and the native fail-closed contract test passes locally.
- Follow-up evidence on the same working tree: `native encoder request fails
  closed*` 1/1, `VideoExecutionResolver*` 3/3 (16 assertions), compiler
  boundary 6/6 (60 assertions), architecture self-test 10/10, and
  `check_no_sqlite.sh` PASS on 2026-09-08.
- The migration is not yet globally CLOSED: Wave 3 direct typed-program
  execution, zero-copy GPU evidence, and same-SHA certification remain open.
- `TypedProgramSampler` is now wired through the prepared typed execution route:
  `JobSession::program()` → `RenderPipeline::render_program()` →
  `FrameExecution`/`RenderGraphContext::frame_parameters`. `JobSession` no
  longer owns `legacy_composition_`; arbitrary `Composition::SceneFunction`
  remains an explicit compatibility adapter until typed Composition lowering is
  available.- Cross-repo Wave 10 — VERIFIED at `RenderingGen main@3c4de1ec` (see `TICKET-STAGE-CLOSING-ARCH-WAVES-9-13`; wave closeout chronicle retired to `docs/tickets/`): Chronon emits `summary.windows` prewarm/steady-state, RenderingGen `preserveRawTimingSidecar` stores raw bytes opaquely (hash→store); no JSON re-parse/mutate/drop — invariant locked, not a reopen.
- Hot-path landed (no-build yet): G41 `reset_glyph_states_in_place` reuse • G43 `RawVideoSink` tight-stride direct `write_bytes` (staging only when padded) • G45 `graph_cache_refresh_fallback_total` • G46-48/H census deferred post-baseline (see `TICKET-COMPILER-DEMOLITION-WAVES` FASE C/D/E/H + `TICKET-FFMPEG-PIPE-SINK-DEMOLITION` F37-F40).

- Baseline inspected before this closeout: `main@eb56a6f6c59e6d5ae3eefa1f1cbd69e3ee1f58d1`.
- **Stage 1 / source consistency:** the Vulkan lifecycle already creates, uses and persists `kernels.pipeline_cache`, but `VulkanKernelStore` had lost the matching `VkPipelineCache` member. This closeout restores one explicit owner, destroys the cache with the kernel store, and adds a regression lock.
- **Frame-slot migration:** structurally closed. Runtime build registration uses `FrameSlotPool`, `GpuCompletionTracker` and `FrameQueue`; the retired `FrameSlotPipeline` path has been deleted. Do not restore the old `FrameExecutionSlotRing` compatibility facade.
- **Clean-checkout certification:** NOT YET CERTIFIED on the resulting SHA until the GitHub `Chronon CI` workflow is green. Source-level inconsistency is fixed here; CI is the same-SHA build/test authority for this checkpoint.
- **No feature expansion:** FrameDelta, Render Ledger, advanced PixelProgram work and further pipeline-cache hardening remain out of this stage.

## Stage 2 — Wave 0 census

| Area | Stato osservato | Closeout |
|---|---|---|
| Surface authority | DONE | `CompiledResourceTable` owns physical placement; Vulkan is materialization/runtime binding only. |
| GraphExecutor | DONE | `GraphExecutor` is the sole production execution authority; helper TUs have distinct responsibilities. |
| Descriptor authority | DONE | `VulkanDescriptorAuthority` owns descriptor allocation/handles; compatibility names in `Impl` are references only. |
| FFmpeg subprocess | DEMOLITION DEBT (scope redefined 2026-09-04) | Negative census: `FfmpegPipeSink`/`ProcessRunner` files and symbols are already gone from src/apps/include. Residual scope is the CLI pipe *encoder* fallback (`VideoSinkEncoderAdapter(Ffmpeg)` + silent native→pipe downgrade in `create_video_encoder`), tracked in `TICKET-FFMPEG-PIPE-SINK-DEMOLITION`. |
| DeviceScheduler | DONE | Runtime implementation and focused tests exist; no second scheduler introduced. |
| CLI typed config / RenderPlan | DONE (focused) | CLI V3 / `PreparedRenderPlan -> CompiledComposition` remains the canonical typed route. |
| Telemetry | DONE (Wave 1D) | Runtime SQLite store + granular emitters + CLI DB readers demolished (2026-09-07): events-only boundary, NullTelemetryStore default, zero sqlite3 dependency in src/include/apps; persistence/aggregation engine-external (host store / observability collector). Record: [TELEMETRY-SQLITE-NORMALIZATION](tickets/TICKET-TELEMETRY-SQLITE-NORMALIZATION.md). |
| Docs | RECONCILED HERE | `CURRENT_STATUS.md`, `CHRONON_PLAN.md`, `FOLLOWUP_TICKETS.md` and the Stage 1/2 ticket now describe the same ordering and blockers. |

**Stage 2 verdict:** structural Wave 0 closeout is complete for the authority migrations already landed. Physical FFmpeg subprocess demolition remains a separate exit-condition-driven ticket. Telemetry compatibility deletion was executed as Wave 1D (2026-09-07): the runtime SQLite store, its gated emitters and the CLI database readers are physically removed; Chronon is events-only and owns no persistence — instrumentation/collection/persistence are separated and engine-external.

## Release / certification state

- Release tag: `v0.1`.
- Historical green baseline: `main@7eb5c2ba` 11/11 PASS (2026-07-06); this is historical evidence, not same-SHA certification of the current main.
- Performance baseline v1 exists and is locked for the software/CPU corpus; GPU-side certification still requires the appropriate build host.
- GPU-native video has demonstrated zero readback/fallback in focused runs, while full zero-copy Vulkan/CUDA/NVENC remains unfinished.
- Current main must not be called green until same-SHA CI/build/test evidence exists.

## Active blockers

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-MAIN-TRUTH-STAGE-1-2 | main/build/docs | P0 VERIFY | [ticket](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) — code/docs closeout landed; same-SHA CI remains the closing gate |
| TICKET-125-TEST-AGGREGATOR | testing | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) |
| TEST-FONT-ASSET-PATH / CERT-SEQUENCE-WBH | testing/cert | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) + [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) — font/asset fixture debt RESOLVED (bootstrap re-pinned 2026-09-05); WBH cert portion remains |
| FFMPEG-PIPE-SINK-DEMOLITION | video cleanup | OPEN | [ticket](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — delete only after all exit conditions are certified |

Indice completo: [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).

## Area summary

| Area | Stato |
|---|---|
| CLI V3 / RenderPlan | PASS (focused) |
| Camera V1 | PARTIAL — same-SHA certification outstanding |
| Text Core V1 | PASS (focused) |
| Text Production | PARTIAL |
| GPU Production V1 | PARTIAL |
| Executor ownership | DONE structurally |
| Video pipeline | PASS (focused); zero-copy partial — Z1 typed feed • Z4 matrix • Z7 CUDA-lane counter • `video_surface_upload_bytes` sidecar • `synchronize_native_output` policy `is_gpu_native_required()` fix landed (ZERO-COPY-ZWAVES), GPU certification pending |
| CI infrastructure | NOT CERTIFIED on current SHA until workflows complete |
| Determinism | PASS on existing focused contracts; formal BitExact contract is a later stage |
| Packaging | PASS (historical/local evidence) |

## Operating rule

Do not advance to new compiler architecture while Stage 1 same-SHA CI is red or unknown. After Stage 2, follow [`CHRONON_PLAN.md`](CHRONON_PLAN.md): fixed CPU overhead -> determinism contract -> single sync authority -> certification baseline -> safe optimization -> new compiler architecture.

## Canonical links

- [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md)
- [`RELEASE_GATE.md`](RELEASE_GATE.md)
- [`CHRONON_PLAN.md`](CHRONON_PLAN.md)
- [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md)
- [`EXECUTOR_OWNERSHIP.md`](EXECUTOR_OWNERSHIP.md)
- WBH toolchain contract: `tools/check_wbh_toolchain.sh` + `tools/wbh_toolchain_manifest.env` (verify presence before use)
