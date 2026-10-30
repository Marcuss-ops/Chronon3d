# Chronon3D — Current Status

Latest benchmark note (2026-10-29): the current-tree Release B2 five-run rerun
encoded 456/456 frames on all runs at a mean 238.703 render-loop fps, +3.40%
over the preceding 230.861 run but still 20.43% below the 300 fps gate.
Evidence is in
`evidence/100x-perf-waves-b2-20261029/b2-final-current-release-20261029-runs.json`.

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
| Remote main (observed 2026-10-29) | `a214fad27efdaa597ccf4e4a8ae34baa6677146e` (matches local HEAD). It includes particle GPU instancing and the Posterize/ColorMap effects. The latest listed Actions batch on this SHA is not green: release artifact succeeded, CI/Developer Gates/CFI and SVG pixel truth were cancelled, and Nightly/CodeQL/runtime/resource-leak/ABI/native-video jobs failed. T05 GPU A/B and same-SHA certification remain open. |

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
without native FFmpeg fail closed. The 2026-10-23 census verifies exit
conditions 2/4/5 (zero production/SDK pipe callers, `video_pipe_fallback_frames = 0`
on canonical strict-native runs, anti-resurrection architecture gate landed at
209/209); the native-certification exit condition (CI green) remains, so
physical deletion stays blocked in
[`TICKET-FFMPEG-PIPE-SINK-DEMOLITION`](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md).
`SoftwareRenderer` remains the active software renderer; its old `runtime()`
compatibility accessor has already been removed and no code callers remain.
The remaining `SoftwareExecutionContext` debt is runtime-header decoupling, as
tracked in [`TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT`](tickets/TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT.md).

## Active blockers

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-MAIN-TRUTH-STAGE-1-2 | main/build/docs | PARTIAL | [ticket](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) — Actions on `a214fad` allocated runners but the latest batch had failures/cancellations; certify a reviewed same-SHA green run |
| CERT-SEQUENCE-WBH | testing/cert | PARTIAL | [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) — protocol, toolchain manifest and local qualification delivered; clean same-SHA certification remains |
| FFMPEG-PIPE-SINK-DEMOLITION | video cleanup | BLOCKED | [ticket](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — exits 2/4/5 verified 2026-10-23 (zero pipe callers, anti-resurrection gate 209/209); exit 3 CI-green evidence outstanding |
| FUSED-STORAGE-IMAGE-DEMOLITION | render graph cleanup | PARTIAL | [ticket](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) — compiler/executor image branch demolished; one same-SHA native-image video certificate is PASS, broader overlay corpus parity remains |

Indice completo: [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).

## Area summary

| Area | Stato |
|---|---|
| Render-plan V3 coherence | PASS on 264b3c3d — v3 extensions dedup + per-key timing + flash hex + fingerprint v10 golden pinned (c_abi 138/138) — see git log 264b3c3dc |
| Lottie Bodymovin lowering | IN PROGRESS — bounded local contract and software CLI/reference gates pass; unsupported Bezier/procedural text is an explicit fail-closed exclusion. Same-SHA CI certification remains open — [ticket](tickets/TICKET-LOTTIE-ANIMATED.md) |
| SVG import | PARTIAL — bounded paths/styles, object-bounding-box gradients with static `gradientTransform` and synchronized two-keyframe stop-color/stop-opacity animation, compile-time local `<use>/<defs>` expansion, path morphs, path/group SMIL transform/opacity tracks, animated path paint, and static clip/vector masks on root or single-path scopes lower through native plans; animated gradient coordinates, mixed clip scopes, nested group transforms and SVG text remain open — [ticket](tickets/TICKET-SVG-RENDER-PLAN.md) |
| Documentary SVG video recreation | IN PROGRESS — imported SVG pixel, sampled trim, marker alignment, synthetic moving-marker glow, text-stroke and deterministic raw gates pass; registry-backed configurable graph-study preset and MP4 render through software CLI. The combined gate reproduces SVG pixel loss without a constant-opacity workaround and verifies the workaround; disabling composite input storage transfer does not change the loss. Underlying cause, exact reference comparison, source-footage composite, target-scene glow and GPU lane remain open — [ticket](tickets/TICKET-SVG-DOCUMENTARY-VIDEO-PARITY.md) |
| Vulkan effect parity | Historical PASS at the archived certification SHA (48/48 native rows; registry 106/106, graph 421/421, effects 275/275, ABI 21/21 on RTX A4000). Current source has 51 native route declarations in the expanded catalog and the CPU-side census passes; Color Matrix local RTX A4000 parity PASS (18/18 assertions; [closed ticket](tickets/archive/TICKET-COLOR-MATRIX-EFFECT.md)); the four new AE effects remain Unsupported — [AE effects ticket](tickets/TICKET-AE-EFFECT-PARITY.md), [historical ticket](tickets/archive/TICKET-VULKAN-EFFECT-PARITY.md) |
| CLI V3 / RenderPlan | PASS (focused) |
| Camera authority | CLOSED — CM3D owns authoring/sampling; Template uses resolved-sample lowering; C3D consumes resolved camera state. Focused value/adapter checks and T16 integration were recorded in the camera-authority ticket, closed and removed 2026-10-27 (history in git). |
| Cinematic compositions T16–T18 | CLOSED FOR CURRENT RELEASE — T16 150-frame strict-native export and raw renderer parity pass. T17/T18 are optional showcase scenes parked until selected for production; codec p99 and same-SHA/CI are not release gates; the optional T17/T18 showcase work was retired with its ticket (removed 2026-10-27). |
| Daemon lifecycle and node-memory metrics | CLOSED FOR CURRENT SCOPE — current daemon admission policy retained; 3-shape GPU suite complete; per-node metrics and local CPU gate implemented. Details were in the daemon-lifecycle / node-memory tickets, closed and removed 2026-10-27 (history in git). |
| Lights / shadows (T05) | PARTIAL — latest current-tree Release no-probe acceptance produced 3/5 complete caster-off and 5/5 caster-on frames; previous batch was 4/5 off and 3/5 on, so off stability regressed and on improved. All renders decoded; process exit is not the image gate. Physical image-alias guards did not fire. Descriptor probe showed distinct Lighting sets/views/images/allocations and valid extents; Transform snapshots were populated while two Lighting snapshots read zero. Focused backend Lighting tests pass 3/3 cases, 69/69 assertions, but do not reproduce the graph issue. Lifetime root cause and shadow-quality acceptance remain open; C2 timings are 2.047/2.607/7.998 s for 1k/10k/100k particles (not kernel-only); GPU instancing/parity and same-SHA CI remain open — [ticket and evidence](tickets/TICKET-LIGHTS-SHADOWS-PARTICLES.md) |
| Render-plan audio mix | PASS (focused) — V3 clips, trim, gain automation, ducking and native mux integration; GPU/CI same-SHA certification remains separate |
| Text Core V1 | PASS (focused) |
| Text depth focus (text_depth_focus_v1) | PARTIAL — resolver and preset recipes are in `ChrononPlugin/plugins/typography`; text animator primitives remain in Chronon3D |
| Text Production V1 | PARTIAL — acceptance contract is in [`RELEASE_GATE.md`](RELEASE_GATE.md): 20 general + 8 subtitle presets, timed JSON/SRT input, productive word styling, per-preset 16:9/9:16 short/long multi-time goldens, installed-SDK use, and serial/parallel determinism. The claimed 20+8 catalog count is not backed by a current inventory artifact found in this checkout; the other end-to-end criteria are not certified. |
| GPU Production V1 | PARTIAL / scope not separately defined — no dedicated ticket or acceptance contract named “GPU Production V1” was found. Related measurable C3D gates are [zero-copy Z-waves](tickets/TICKET-ZERO-COPY-ZWAVES.md), [GPU corpus](tickets/TICKET-GPU-BASELINE-CORPUS.md), and the GPU-native video evidence above; the content corpus and broad release certification remain open. |
| Executor ownership | DONE structurally |
| Agent visual finishing (software) | PARTIAL — RenderPlan color contract and optional mastering facts are wired through native video export; CPU scopes analyze decoded video and report PQ peak measurements. Runtime probes verified PQ H.264 High 10, PQ H.265 Main 10, and HLG H.265 Main 10 as 10-bit BT.2020-NCL limited-range outputs with correct transfer and mastering/CLL stream metadata. An OCIO display/view still probe preserved transparent and half-alpha pixels with transformed color. The local ONNX semantic matte provider passed gates on a real 640×360 clip frame; four-frame CPU temporal propagation published accepted mattes at 80×45. The focused encoder suite passes 32/32 cases (1,607 assertions). Representative project footage matte review, broader color/alpha edge coverage, and full project verification remain open — [ticket](tickets/TICKET-AGENT-VISUAL-FINISHING.md) |
| Video pipeline | PASS (focused); same-SHA GPU certification at `5e49b7e4a` covers FullGraph zero-copy, DirectYUV conversion-zero, MTSDF overlay and native-image video. Final local zero-copy suite and CUDA NV12 6/6 PASS. Latest B2 title-drift rerun: five runs, 456/456 encodes each, mean 238.703 render-loop fps (<300; +3.40% vs preceding 230.861), drain 12.8 µs/job; active-sample `nvidia-smi` encoder utilization 98.48%, canonical sidecar null, stable output SHA. B2 remains not accepted. |
| CI infrastructure | PARTIAL — latest listed Actions batch on `a214fad` allocated runners; release artifact passed, Chronon CI/Developer Gates/CFI were cancelled, and Chronon Nightly/CodeQL/runtime/resource-leak failed. Local fixes cover the `write()` warning, two Linux Release test compile defects and a stale SDK header-install path. On 2026-10-29 the targeted current-tree build succeeded after staging the 3.27 GB static SDK archive in `/dev/shm`; 10 focused CTest suites pass after correcting the catalog census (57 effects, 51 native, 6 explicit fail-closed). SDK shared-library link and C ABI test now pass locally; latest T05 acceptance is 3/5 off and 5/5 on, with root cause open; latest B2 mean is 238.703 fps, below 300; same-SHA CI remains external — [ticket](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md). |
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
- 2026-10-23 — docs/evidence reconciliation at `f935e3e32`: CAM-04 same-plan pixel parity (five frames, MAE 1.429–1.592, p99 20–21), the focused frame-75 90°/8-sample shutter-pixel gate (MAE 1.457, p99 5), the four-background matte review and the T05 C3D shadow gate (PARTIAL, strict-native fail-closed) already existed as committed evidence but were missing from this status file; the camera, cinematic-composition and lights/shadows rows are updated to the evidence-true state on that SHA.
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
