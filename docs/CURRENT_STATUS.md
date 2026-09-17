# Chronon3D — Current Status

> **Single v0.1 release contract:** [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md). This file reports observed state; it does not redefine release identity.
>
> **Observed state only.** Per AGENTS.md, extended chronicle lives in tickets: this file
> carries at most one synthetic line per closed checkpoint. The dated closeout narrative
> that had accumulated here (2026-09-04, 2026-09-08, 2026-09-11 ×2) is deliberately not
> kept in any document: closed checkpoints live in the git history (`git log --follow`),
> which is the only archive.

## Release / certification state

| Fact | Observed value |
|---|---|
| Release tag | `v0.1` |
| Historical green baseline | `main@7eb5c2ba` 11/11 PASS (2026-07-06) — historical evidence, NOT same-SHA certification of current main |
| Performance baseline | v1 exists and is locked for the software/CPU corpus; GPU-side certification still requires the appropriate build host |
| GPU-native video | full zero-copy Vulkan/CUDA/NVENC measured **PASS** on the FullGraph lane (2026-09-16: `ZERO_COPY_PASS` 16/16, zero staging copy, zero D2D blit, zero readback); the DirectYUV conversion-zero certificate still needs a video-source plan |
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

Open authority migration: the explicit FFmpeg CLI pipe *encoder* path
(`VideoSinkEncoderAdapter(Ffmpeg)`) remains **demolition debt**. A native request
without native FFmpeg now fails closed; it must never silently downgrade to pipe.
Delete the explicit diagnostic path only after all exit conditions are certified,
tracked in [`TICKET-FFMPEG-PIPE-SINK-DEMOLITION`](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md).
The legacy `SoftwareRenderer` runtime compatibility surface still has callers and
`SoftwareExecutionContext` is therefore PARTIAL, not a zero-back-pointer closeout.

## Active blockers

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-MAIN-TRUTH-STAGE-1-2 | main/build/docs | P0 VERIFY | [ticket](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) — code/docs closeout landed; same-SHA CI remains the closing gate |
| TICKET-125-TEST-AGGREGATOR | testing | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) |
| TEST-FONT-ASSET-PATH / CERT-SEQUENCE-WBH | testing/cert | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) + [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) — font/asset fixture debt RESOLVED (bootstrap re-pinned 2026-09-05); WBH cert portion remains |
| FFMPEG-PIPE-SINK-DEMOLITION | video cleanup | OPEN | [ticket](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — delete only after all exit conditions are certified |
| FUSED-STORAGE-IMAGE-DEMOLITION | render graph cleanup | PARTIAL | [ticket](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) — compiler/executor image branch demolished; native-image overlay/video certification still open |

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
| Video pipeline | PASS (focused); zero-copy **certified on host GPU** for the FullGraph/NVENC lane (`tools/verify_zero_copy_end_to_end.sh` → `ZERO_COPY_PASS`, 16 PASS / 0 FAIL, 2026-09-16, counters in [`ZERO_COPY_GATES.md`](ZERO_COPY_GATES.md)); the same tool with `--direct-yuv` on the builtin procedural scene downgrades in a declared, counted way (`direct_yuv_capability_rejected`) because it needs a video source, so the conversion-zero certificate is still open. Z1 typed feed • Z4 matrix • Z7 CUDA-lane counter • `video_surface_upload_bytes` sidecar landed (ZERO-COPY-ZWAVES) |
| CI infrastructure | NOT CERTIFIED on current SHA until workflows complete |
| Determinism | PASS on existing focused contracts; formal BitExact contract is a later stage |
| Packaging | PASS (historical/local evidence) |

## Closed checkpoints (chronicle index)

One synthetic line per closed checkpoint; detail lives in the named evidence
documents and the git history.

- 2026-09-04 — main truth checkpoint: no narrative recorded.
- 2026-09-08 — migration closeout: Wave 4 compiler caller census CLOSED; native encoder contract fail-closed; migration not globally CLOSED (Wave 3 typed execution, zero-copy GPU evidence, same-SHA certification open).
- 2026-09-11 — Phase 4/5 evidence: Vulkan independent jobs PASS, framebuffer contention PASS (preserve), concurrent determinism PASS, steady-state allocation budget **historical PASS** at `0.0` (the committed report is pinned to `8bdf0f807` and is not current-SHA certification). See [`evidence/phase4-determinism.md`](evidence/phase4-determinism.md) and [`evidence/phase5-allocation-budget.md`](evidence/phase5-allocation-budget.md).
- 2026-09-11 — certification harness: B00 native smoke on RTX A4000/NVENC, 120/120 native frames, zero CPU readback; official corpus blocked on a content-pack build (`CHRONON3D_BUILD_CONTENT=OFF`). See [`evidence/gpu-b00-native-smoke-a16c86de2.md`](evidence/gpu-b00-native-smoke-a16c86de2.md).
- 2026-09-11 — FrameDelta compiler: integrated production path for dirty collection, tile policy and scene-tile execution; `chronon3d_render_graph_tests` 1/1 on the current main SHA.
- 2026-09-16 — zero-copy certification on the host GPU (`ZERO_COPY_PASS`, FullGraph/NVENC lane); the closed-vocabulary fix that extended the `changed_intervals` entry check to the `output_spec` scope (`chronon3d_render_job_contract_tests` 15/15); the registry suite closed at **59/59** by wiring a hand-built `RenderGraphContext` the way the production authority does; and three fixes to the public-header gate so CI Gate 3 can actually run (stale `--headers-list` invocation, relative generated-TU path handed to a compiler running in the build tree, flag template taken from a generator TU without `-I<repo>/include`). The video-lane tests tree was re-created at `build/chronon/linux-video-test`; the build-time defect that made every `ninja` invocation re-run all edges and the `-j12` resource budget are documented in [`CCACHE-AUDIT-2026-09-10.md`](CCACHE-AUDIT-2026-09-10.md).

- 2026-09-16 — text tile canary **CHIUSO 78/78** (era 65/78): il rosso era un **binario stantio** (`.ninja_log` 07:00 UTC vs ABI fix `5b59cfd48` 16:33 UTC, in un target che nessun gate ricostruiva), non un contratto rotto né un difetto driver/GPU — vedi [`evidence/text-tile-canary-2026-09-16.md`](evidence/text-tile-canary-2026-09-16.md). Promosso a gate GPU/WBH `tools/verify_text_tile_gpu_canary.sh`. Contestualmente il **raw sink** pre-encode (`--video-sink raw`, `2a5579985`) è atterrato come superficie di verità pixel per la lane video, e il difetto residuo della via immagine (PNG still/sequenza) è stato chiuso con la dichiarazione di lane non-canonica (profilo in [`evidence/image-path-defect-2026-09-16.md`](evidence/image-path-defect-2026-09-16.md); scheda in git history).

## Operating rule

Do not advance to new compiler architecture while Stage 1 same-SHA CI is red or unknown. After Stage 2, follow [`CHRONON_PLAN.md`](CHRONON_PLAN.md): fixed CPU overhead -> determinism contract -> single sync authority -> certification baseline -> safe optimization -> new compiler architecture.

## Canonical links

- [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md)
- [`RELEASE_GATE.md`](RELEASE_GATE.md)
- [`CHRONON_PLAN.md`](CHRONON_PLAN.md)
- [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md)
- [`EXECUTOR_OWNERSHIP.md`](EXECUTOR_OWNERSHIP.md)
- WBH protocol (P1, OPEN): the gate groups live in `tools/gates/manifest.sh` (`WBH_GATES`); the protocol doc and the toolchain-manifest contract are still to be delivered — see [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md)
