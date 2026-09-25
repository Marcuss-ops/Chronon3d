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
| GPU-native video | same-SHA host certification at source commit `5e49b7e4a`: Vulkan↔CUDA interop and CUDA→NVENC probes PASS; FullGraph, DirectYUV H.264/H.265, prepared-MTSDF text, and native-image routes PASS with zero readback/staging; GitHub CI remains separate |
| GPU text route | packed compatibility route CLOSED 2026-09-22; prepared MTSDF is the only native graph route, with explicit software/reject handling |
| Current main | NOT CERTIFIED — all GitHub Actions jobs for `0674ac27` were blocked before runner startup on 2026-09-25 because account payments failed or the spending limit was reached; same-SHA CI/build/test evidence is still required |

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
| FUSED-STORAGE-IMAGE-DEMOLITION | render graph cleanup | PARTIAL | [ticket](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) — compiler/executor image branch demolished; one same-SHA native-image video certificate is PASS, broader overlay corpus parity remains |

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
| Video pipeline | PASS (focused); same-SHA GPU certification at `5e49b7e4a` covers FullGraph zero-copy, DirectYUV conversion-zero on H.264/H.265 video-source fixtures, visual MTSDF text overlay, and Vulkan native-image video. Z1 typed feed • Z4 matrix • Z7 CUDA-lane counter • `video_surface_upload_bytes` sidecar landed (ZERO-COPY-ZWAVES) |
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

- 2026-09-24 — `MeshShape::mesh` demolition **CHIUSO**: il compatibility escape hatch in-memory è rimosso (`include/chronon3d/scene/model/shape/shape.hpp`); `MeshShape` porta solo `reference` + `prepared`. Tutti i consumatori sono migrati: `software_mesh_processor.cpp` (draw/bbox), `graph_builder_source_pass.cpp` e il check in `tests/assets/test_asset_manifest.cpp`. Verifica: `chronon3d_core_tests` verde sui casi mesh/asset (86 casi / 332 assert); nessun fallimento mesh nei suite confinanti (i rossi residui sono font/text d'ambiente). Scheda rimossa a chiusura terminale.
- 2026-09-24 — daemon IPC rejection resilience **CHIUSO**: un rigetto tipato di capability non deve uccidere il daemon (era stato osservato `ipc read reply: EOF` + restart del servizio). Il contenimento (`dispatch_request_safely`) è coperto da un test di integrazione sul transport reale (`tests/cli/test_daemon_ipc_exception_boundary.cpp`: rigetto → error envelope → `STATUS` Ok → render valido successivo → `Shutdown` pulito); `chronon3d_cli_tests` 82/82 casi, 1084/1084 assert.
- 2026-09-24 — Vulkan animated-image residency **CHIUSO**: pixel probe 12/12 e strict capability routing (`full_graph_native`/`native_surface`, zero readback; animated strict respinto esplicito) certificati su RTX A4000; scheda rimossa a chiusura terminale (git history = archivio), regressioni permanenti in `tests/fixtures/animated_image_residency/run_pixel_probe.sh` e `tests/registry/test_backend_registry_vulkan_transforms.cpp`.
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
