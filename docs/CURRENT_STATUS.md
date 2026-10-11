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
| Strict motion corpus (2026-10-10) | 479/479 PASS same-tree (`e3f14fbf4`+wave / RG `5e1b7f5`+harness): web_rect 12, light_leak 12, entity_caption 56, image 126, phrase 203, presentation 70 — strict vulkan/nvenc/native/require_gpu_native, h264 1280x720 48f; clean-SHA re-cert owed after commit |
| GPU text route | packed compatibility route CLOSED 2026-09-22; prepared MTSDF is the only native graph route, with explicit software/reject handling |
| Remote main (observed 2026-10-11) | `e8e06f5b3` — push run [38118149793](https://github.com/Marcuss-ops/Chronon3d/actions/runs/38118149793) and Chronon Nightly [38129454332](https://github.com/Marcuss-ops/Chronon3d/actions/runs/38129454332) fail before runner execution (`steps: []`); no source-level CI result is available. |

## Area authority census

`CompiledResourceTable` owns physical placement (Vulkan is materialization/runtime binding
only); `GraphExecutor` is the sole production execution authority with helper TUs holding
distinct responsibilities; `VulkanDescriptorAuthority` owns descriptor allocation/handles;
`FrameSlotPool` + `GpuCompletionTracker` + `FrameQueue` are the runtime build registration
(no second scheduler, and the retired `FrameSlotPipeline` / `FrameExecutionSlotRing`
facades must not return); CLI V3 / `PreparedRenderPlan -> CompiledComposition` is the
canonical typed route; telemetry is events-only with `NullTelemetryStore` as default and no
sqlite3 dependency in `src`/`include`/`apps`.

Focus gate: freeze new product features until the current Chronon3D revision has
green GitHub CI on its exact SHA. Keep uncommitted work local until it has been
reviewed and can be certified as one identifiable revision.

Open authority migration: the explicit FFmpeg CLI pipe *encoder* path
(`VideoSinkEncoderAdapter(Ffmpeg)`) remains **demolition debt**. Native requests
without native FFmpeg fail closed. Pipe callers and native certification exit
conditions remain, so physical deletion stays blocked in
[`TICKET-FFMPEG-PIPE-SINK-DEMOLITION`](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md).
`SoftwareRenderer` remains the active software renderer; its old `runtime()`
compatibility accessor has already been removed and no code callers remain.
The remaining `SoftwareExecutionContext` debt is runtime-header decoupling, as
tracked in [`TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT`](tickets/TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT.md).

## Active blockers

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-MAIN-TRUTH-STAGE-1-2 | main/build/docs | BLOCKED | [ticket](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) — remote Chronon3D jobs do not allocate a runner; restore runner dispatch, then certify the reviewed SHA |
| TEST-FONT-ASSET-PATH / CERT-SEQUENCE-WBH | testing/cert | PARTIAL | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) + [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) — font/asset fixture debt RESOLVED; protocol and toolchain manifest delivered; clean same-SHA WBH certification remains |
| FFMPEG-PIPE-SINK-DEMOLITION | video cleanup | BLOCKED | [ticket](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — native CI and production caller exit conditions are not certified |
| FUSED-STORAGE-IMAGE-DEMOLITION | render graph cleanup | PARTIAL | [ticket](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) — compiler/executor image branch demolished; one same-SHA native-image video certificate is PASS, broader overlay corpus parity remains |

Indice completo: [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).

## Area summary

| Area | Stato |
|---|---|
| Render-plan V3 coherence | PASS on 264b3c3d — v3 extensions dedup + per-key timing + flash hex + fingerprint v10 golden pinned (c_abi 138/138) — see git log 264b3c3dc |
| Lottie Bodymovin lowering | PARTIAL — L1 transforms, L2 nested groups, L3 static features, matching-topology multi-keyframe L4 morphs, L5 Trim Paths, L6 animated solid paint, bounded L8 group transforms, and L9/L10 masks with animated opacity and bounded two-keyframe path morphs lower through existing plans; L1/L5 CLI frame-hash gates pass, later importer features remain open — [ticket](tickets/TICKET-LOTTIE-ANIMATED.md) |
| SVG import | PARTIAL — bounded paths/styles, object-bounding-box gradients with static `gradientTransform` and synchronized two-keyframe stop-color/stop-opacity animation, compile-time local `<use>/<defs>` expansion, path morphs, path/group SMIL transform/opacity tracks, animated path paint, and static clip/vector masks on root or single-path scopes lower through native plans; animated gradient coordinates, mixed clip scopes, nested group transforms and SVG text remain open — [ticket](tickets/TICKET-SVG-RENDER-PLAN.md) |
| Vulkan effect parity | CLOSED — catalog capability table has 48 rows, all 48 strict-native (0 unsupported); strict no-fallback policy holds. Same-tree evidence on the RTX A4000 host: registry 106/106, graph 421/421, effects 275/275, ABI 21/21, incl. software/Vulkan pixel oracles for all 22 former unsupported effects — [ticket](tickets/TICKET-VULKAN-EFFECT-PARITY.md) |
| CLI V3 / RenderPlan | PASS (focused) |
| Camera V1 | PARTIAL — `SceneBuilder::camera()` / `CameraApi` authoring was removed; static cameras use `camera_descriptor()` and animated poses use `camera_pose()`; same-SHA certification remains outstanding |
| Render-plan audio mix | PASS (focused) — V3 clips, trim, gain automation, ducking and native mux integration; GPU/CI same-SHA certification remains separate |
| Text Core V1 | PASS (focused) |
| Text depth focus (text_depth_focus_v1) | PASS (unit) — `TextFocusResolver` authority + 8 preset family ids landed header-only over the canonical animator primitives; suite `chronon3d_text_focus_resolver_tests` 16/16 |
| Text Production | PARTIAL |
| GPU Production V1 | PARTIAL |
| Executor ownership | DONE structurally |
| Agent visual finishing (software) | PARTIAL — RenderPlan color contract and optional mastering facts are wired through native video export; CPU scopes analyze decoded video and report PQ peak measurements. Runtime probes verified PQ H.264 High 10, PQ H.265 Main 10, and HLG H.265 Main 10 as 10-bit BT.2020-NCL limited-range outputs with correct transfer and mastering/CLL stream metadata. An OCIO display/view still probe preserved transparent and half-alpha pixels with transformed color. The local ONNX semantic matte provider passed gates on a real 640×360 clip frame; four-frame CPU temporal propagation published accepted mattes at 80×45. The focused encoder suite passes 32/32 cases (1,607 assertions). Representative project footage matte review, broader color/alpha edge coverage, and full project verification remain open — [ticket](tickets/TICKET-AGENT-VISUAL-FINISHING.md) |
| Video pipeline | PASS (focused); same-SHA GPU certification at `5e49b7e4a` covers FullGraph zero-copy, DirectYUV conversion-zero on H.264/H.265 video-source fixtures, visual MTSDF text overlay, and Vulkan native-image video. Z1 typed feed • Z4 matrix • Z7 CUDA-lane counter • `video_surface_upload_bytes` sidecar landed (ZERO-COPY-ZWAVES) — plus 264b3c3d local same-SHA for render-plan layer of pipeline |
| CI infrastructure | BLOCKED — current main’s Actions jobs stop before runner allocation (`steps: []`); owner-side runner billing/spending limits must be restored before same-SHA certification — [ticket](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md). |
| Determinism | PARTIAL — `BitExact` / `DeterministicWithinPlatform` / `Approximate` types and focused fusion certification tests exist; broad cross-backend scene certification remains open |
| Premium effects brush pressure | PASS (local) — pressure expectation corrected to the documented contract; `chronon3d_effects_tests` 275/275 green |
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
- 2026-09-26 — render-plan v3 coherence **CHIUSO a 264b3c3d**: dedup `layer_transition`+`transition_in/out` last-wins, per-keyframe `interpolation/easing/bezier_curve` su `layers[].animation`, flash `color` hex `#RRGGBB`→linear via `parse_hex_color`, golden fingerprint v9→v10 `7068117211625980799`; `c_abi 138/138` e `cli 85/85` verdi, `arch 202/202` e `6/6` fast gates verdi sullo stesso SHA — vedi `git show 264b3c3dc`.
- 2026-10-10 — pipeline, text engine & compiler boundary fixes **CHIUSI a 68839712d**: animazione luci V3 (position/direction tracks, `c_abi` 269/269), lock sharding Vulkan, priorità deterministica font fallback arabi (text 18/18 test suite verdi), sincronizzazione schema sidecar timing (`cli` 251/251) e 6/6 developer gates (206/206 arch rules) verdi sullo stesso SHA.

## Operating rule

Do not advance to new compiler architecture while Stage 1 same-SHA CI is red or unknown. After Stage 2, follow [`CHRONON_PLAN.md`](CHRONON_PLAN.md): fixed CPU overhead -> determinism contract -> single sync authority -> certification baseline -> safe optimization -> new compiler architecture.

## Canonical links

- [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md)
- [`RELEASE_GATE.md`](RELEASE_GATE.md)
- [`CHRONON_PLAN.md`](CHRONON_PLAN.md)
- [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md)
- [`EXECUTOR_OWNERSHIP.md`](EXECUTOR_OWNERSHIP.md)
- WBH protocol (P1, PARTIAL): protocol is [documented](CERT-SEQUENCE-WBH-PROTOCOL.md), manifest and checker are `tools/wbh_toolchain_manifest.env` and `tools/check_wbh_toolchain.py`; local toolchain passes, while clean same-SHA gates remain unrun — see [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md)
