# Chronon3D — Current Status

> **Single v0.1 release contract:** [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md). This file reports observed state; it does not redefine release identity.
>
> **Observed state only.** Per AGENTS.md, extended chronicle lives in tickets: this file
> carries at most one synthetic line per closed checkpoint. The dated closeout narrative
> that had accumulated here (2026-09-04, 2026-09-08, 2026-09-11 ×2) was moved verbatim to
> [`tickets/TICKET-STATUS-CHRONICLE-ARCHIVE.md`](tickets/TICKET-STATUS-CHRONICLE-ARCHIVE.md).

## Release / certification state

| Fact | Observed value |
|---|---|
| Release tag | `v0.1` |
| Historical green baseline | `main@7eb5c2ba` 11/11 PASS (2026-07-06) — historical evidence, NOT same-SHA certification of current main |
| Performance baseline | v1 exists and is locked for the software/CPU corpus; GPU-side certification still requires the appropriate build host |
| GPU-native video | zero readback/fallback demonstrated in focused runs; full zero-copy Vulkan/CUDA/NVENC remains unfinished |
| Current main | must not be called green until same-SHA CI/build/test evidence exists |

## Area authority census

`CompiledResourceTable` owns physical placement (Vulkan is materialization/runtime binding
only); `GraphExecutor` is the sole production execution authority with helper TUs holding
distinct responsibilities; `VulkanDescriptorAuthority` owns descriptor allocation/handles;
`FrameSlotPool` + `GpuCompletionTracker` + `FrameQueue` are the runtime build registration
(no second scheduler, and the retired `FrameSlotPipeline` / `FrameExecutionSlotRing`
facades must not return); CLI V3 / `PreparedRenderPlan -> CompiledComposition` is the
canonical typed route; telemetry is events-only with `NullTelemetryStore` as default and no
sqlite3 dependency in `src`/`include`/`apps`.

Open authority migration: the FFmpeg CLI pipe *encoder* fallback
(`VideoSinkEncoderAdapter(Ffmpeg)` plus the native→pipe downgrade path) is active
**demolition debt** — delete only after all exit conditions are certified, tracked in
[`TICKET-FFMPEG-PIPE-SINK-DEMOLITION`](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md). The
legacy `SoftwareRenderer` runtime compatibility surface still has callers and
`SoftwareExecutionContext` is therefore PARTIAL, not a zero-back-pointer closeout.

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

## Closed checkpoints (chronicle index)

One synthetic line per closed checkpoint; detail in the archive ticket and the named
evidence documents.

- 2026-09-04 — main truth checkpoint: no narrative recorded. See [`tickets/TICKET-STATUS-CHRONICLE-ARCHIVE.md`](tickets/TICKET-STATUS-CHRONICLE-ARCHIVE.md).
- 2026-09-08 — migration closeout: Wave 4 compiler caller census CLOSED; native encoder contract fail-closed; migration not globally CLOSED (Wave 3 typed execution, zero-copy GPU evidence, same-SHA certification open). See [`tickets/TICKET-STATUS-CHRONICLE-ARCHIVE.md`](tickets/TICKET-STATUS-CHRONICLE-ARCHIVE.md).
- 2026-09-11 — Phase 4/5 evidence: Vulkan independent jobs PASS, framebuffer contention PASS (preserve), concurrent determinism PASS, steady-state allocation budget PASS at `0.0`. See [`evidence/phase4-determinism.md`](evidence/phase4-determinism.md) and [`evidence/phase5-allocation-budget.md`](evidence/phase5-allocation-budget.md).
- 2026-09-11 — certification harness: B00 native smoke on RTX A4000/NVENC, 120/120 native frames, zero CPU readback; official corpus blocked on a content-pack build (`CHRONON3D_BUILD_CONTENT=OFF`). See [`evidence/gpu-b00-native-smoke-a16c86de2.md`](evidence/gpu-b00-native-smoke-a16c86de2.md).
- 2026-09-11 — FrameDelta compiler: integrated production path for dirty collection, tile policy and scene-tile execution; `chronon3d_render_graph_tests` 1/1 on the current main SHA. See [`tickets/TICKET-STATUS-CHRONICLE-ARCHIVE.md`](tickets/TICKET-STATUS-CHRONICLE-ARCHIVE.md).

## Operating rule

Do not advance to new compiler architecture while Stage 1 same-SHA CI is red or unknown. After Stage 2, follow [`CHRONON_PLAN.md`](CHRONON_PLAN.md): fixed CPU overhead -> determinism contract -> single sync authority -> certification baseline -> safe optimization -> new compiler architecture.

## Canonical links

- [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md)
- [`RELEASE_GATE.md`](RELEASE_GATE.md)
- [`CHRONON_PLAN.md`](CHRONON_PLAN.md)
- [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md)
- [`EXECUTOR_OWNERSHIP.md`](EXECUTOR_OWNERSHIP.md)
- WBH protocol (P1, OPEN): the gate groups live in `tools/gates/manifest.sh` (`WBH_GATES`); the protocol doc and the toolchain-manifest contract are still to be delivered — see [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md)
