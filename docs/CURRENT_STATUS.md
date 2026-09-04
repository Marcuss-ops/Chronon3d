# Chronon3D — Current Status

> **Single v0.1 release contract:** [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md). This file reports observed state; it does not redefine release identity.

## Main truth checkpoint — 2026-09-04

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
| FFmpeg subprocess | DEMOLITION DEBT | Native path is canonical in native builds, but `FfmpegPipeSink` removal remains blocked by explicit SDK/CI/zero-caller exit conditions. Do not delete early. |
| DeviceScheduler | DONE | Runtime implementation and focused tests exist; no second scheduler introduced. |
| CLI typed config / RenderPlan | DONE (focused) | CLI V3 / `PreparedRenderPlan -> CompiledComposition` remains the canonical typed route. |
| Telemetry | PARTIAL / CONTAINED | Default-path recording was gated behind the real SQLite consumer; physical deletion of remaining compatibility stores is deferred cleanup, not a reason to add another telemetry authority. |
| Docs | RECONCILED HERE | `CURRENT_STATUS.md`, `CHRONON_PLAN.md`, `FOLLOWUP_TICKETS.md` and the Stage 1/2 ticket now describe the same ordering and blockers. |

**Stage 2 verdict:** structural Wave 0 closeout is complete for the authority migrations already landed. Physical FFmpeg subprocess demolition remains a separate exit-condition-driven ticket; telemetry compatibility deletion remains deferred. Neither is allowed to trigger a parallel subsystem.

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
| TICKET-DEPRECATED-API-REMOVAL | API | OPEN | [DEPRECATED-API-REMOVAL](tickets/TICKET-DEPRECATED-API-REMOVAL.md) |
| TEST-FONT-ASSET-PATH / CERT-SEQUENCE-WBH | testing/cert | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) + [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
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
| Video pipeline | PASS (focused), zero-copy still partial |
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
- [`WORKING_BUILD_HOST.md`](WORKING_BUILD_HOST.md)
