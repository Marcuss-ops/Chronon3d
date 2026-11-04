# Chronon — Operating Plan

> Current observed state lives in [`CURRENT_STATUS.md`](CURRENT_STATUS.md). This plan defines ordering, not release certification.

## Principle

Chronon is a deterministic media compiler/runtime. Stop expanding architecture while the current `main` is not buildable and same-SHA certified. Every change must preserve one authority per concern, measurable correctness, and explicit demolition conditions.

No branches are required by this plan; work lands on `main` only after the relevant gate for the change is satisfied.

---

## STAGE 1 — Make main true

**Status: implementation closeout landed in this checkpoint; same-SHA CI verification remains required.**

- P0 `main` self-contained/buildable.
- Finish frame-slot migration without restoring retired compatibility facades.
- Pipeline-cache consistency: one `VkPipelineCache` owner in `VulkanKernelStore`; lifecycle create/use/persist/destroy must agree.
- No new feature/refactor work while a clean-checkout compile defect is known.

Exit gate: `Chronon CI` green on the exact resulting `main` SHA.

## STAGE 2 — Close cleanup

**Status: structural Wave 0 closeout complete; explicit demolition/certification debt remains tracked separately.**

Census order:

1. Surface authority.
2. GraphExecutor closeout.
3. Descriptor authority.
4. FFmpeg subprocess closeout.
5. DeviceScheduler.
6. CLI typed config.
7. Telemetry.
8. Docs reconciliation.

Rules:

- `CompiledResourceTable` owns physical placement; Vulkan materializes/binds it.
- Do not recreate duplicate surface, executor, descriptor, scheduler, config or telemetry authorities.
- `FfmpegPipeSink` physical removal is controlled by `TICKET-FFMPEG-PIPE-SINK-DEMOLITION`; the compatibility path must not be deleted before its SDK/CI/zero-caller exit conditions are certified.
- Deferred compatibility deletion is not permission to create a replacement subsystem.

---

## STAGE 3 — Kill fixed CPU overhead

- [x] `LruCache` detailed nanosecond timing is opt-in; the default path avoids
  clock reads and timing atomics.
- [x] `clear()` advances the in-flight generation so an older loader cannot
  republish a value after clear.
- [x] Oversized-rejection counters are atomic and coalesced waiters have a
  separate tested count.
- [x] Compiled operations are indexed by node id in a vector for O(1) lookup.
- [x] Fine-grained per-node timing is retired; mutable node context and memory
  scope are worker-local.

Correctness evidence: LruCache unit/stress tests, compiled-frame-program
contract tests, executor level tests, and the developer architecture gate.
Benchmark evidence for the normal/rendering hotspot work remains under
[`TICKET-HOTSPOT-DECOMPOSITION`](tickets/TICKET-HOTSPOT-DECOMPOSITION.md);
daemon job reuse remains data-gated in
`TICKET-DAEMON-JOB-LIFECYCLE` (scheda chiusa e rimossa il 2026-10-27; storico in git).

## STAGE 4 — Determinism contract

Define the supported classes explicitly:

- `BitExact`.
- `DeterministicWithinPlatform`.
- `Approximate`.

For `BitExact`, fused and unfused output hashes must match. A 1-ULP tolerance is not bit-exact.

Gate: deterministic contract documented and fusion certification tests enforce it.

## STAGE 5 — Single synchronization authority

Target flow:

`Compiler -> ResourceUse -> ResourceStateTracker -> ResourceTransition[] -> Vulkan Sync2 adapter`

Sequence: census -> contract -> Sync2 adapter -> equivalence -> production switch -> legacy demolition -> Vulkan/CUDA sync -> certification.

Gate: one synchronization authority; legacy barrier/layout guessing removed from production paths.

---

## CHECKPOINT — Clean core

Do not advance until all are true on the same SHA:

- `main` builds clean.
- Tests/gates required for the touched areas are green.
- Duplicate authorities = 0 for surface/descriptor/sync/executor concerns.
- Compatibility debt is documented with explicit exit conditions.
- Fixed cache/executor CPU tax addressed and benchmarked.
- Determinism contract documented.

---

## STAGE 6 — Certification

- Clean checkout and clean release build.
- Runtime/resource/executor/Vulkan/CUDA/video/text tests.
- DirectYUV and multi-video checks.
- Production 5-clip benchmark.
- Record metrics, benchmark outputs and artifact hashes.

Video certification must observe the native path and record at minimum readback/fallback/conversion counters. Do not infer zero-copy from architecture alone.

Result: a clean baseline SHA that becomes the performance reference point.

## STAGE 7 — Safe optimization

- Persistent Vulkan pipeline-cache hardening: device/vendor/UUID keying, shader ABI fingerprint, corruption handling, atomic save, concurrent-daemon safety.
- ThinLTO experiment in release build; keep only with measured benefit and reproducibility intact.
- Golden diff tooling.
- Deterministic property/fuzz pipeline with seeded generation and minimized regression artifacts.
- CUDA Graph experiment only after the clean baseline.

No new generic cache/registry/queue/sync authority may be introduced here.

## STAGE 8 — New compiler architecture

Order:

1. `FrameDelta`.
2. Sparse changed regions/tiles.
3. Canonical `PixelProgram` IR with backend lowering.

`FrameDelta` precedes Render Ledger because incremental work needs a deterministic changed-work model first.

## STAGE 9 — Incremental product

- `RenderLedger` as a manifest/fingerprint product, not another cache.
- Incremental render API based on plan/frame/node/subgraph/artifact fingerprints.

Gate: unchanged work is provably reused without weakening correctness/determinism.

## STAGE 10 — AOT

Introduce `.chronon` only as a render executable containing compiled graph/resource/program/shader/asset/capability/determinism metadata.

It is not a project-editor file format.

## STAGE 11 — Advanced hardware

Only after earlier stages are certified:

- Multi-queue.
- Sparse residency.
- Multi-GPU.

---

## Explicit freeze until prerequisites are met

Do not start:

- ML rendering.
- New generic libraries/caches/registries/queue primitives/sync authorities.
- Multi-GPU spatial work.
- Sparse Vulkan residency.
- NVENC slice hacking.
- Large refactors without benchmark evidence.
- Render Ledger before FrameDelta.
- Advanced fusion before the BitExact contract.

## Definition of Done

A task is not done because code exists. Close it only with the relevant combination of implementation, caller census, negative census, regression test, architecture/developer gate, benchmark for hot-path work, docs reconciliation, clean status, and verification that the intended commit is actually the `main` tip.

---

## Proposed product plan — Chronon 2.0 (2026-11-03)

This is a **proposed cross-repository backlog and release contract**, not a
report that its tasks have been completed or certified. It does not override
the observed state in `CURRENT_STATUS.md`, the current v0.1 release contract,
or repository-specific agent rules. The supplied planning snapshot names C3D
`7fb9fd0a`, CM3D `b7137ee0`, and ChrononTemplate `80959bbc`. The workspace
observed during plan entry has C3D at `b1b21cbe` and CM3D/Template at the
listed SHAs; ChrononTemplate also has untracked files. Resolve this divergence
and record clean commit identities before establishing a release baseline.
RenderingGen is the product orchestrator by proposal; its public repository
was not verified in this reconnaissance.

### Mission and scope freeze

Chronon 2.0 is a headless native C++20 motion-graphics engine for 2D/2.5D
compositions and a bounded 3D subset, MP4 and raw-frame export, repeatable
templates, and one animated camera authored/evaluated by CM3D. Out of scope:
an After Effects equivalent, universal `.aep` support, a game engine, a ray
tracer, all glTF extensions, and polygonal bokeh unless certified.

Non-negotiable contracts:

- CM3D owns authored time, hierarchy, rigs and camera. C3D retains projection,
  occlusion and pixel-rendering physics.
- RenderPlan V3 is the handoff contract; `CompiledProgram` is the prepared
  runtime program. Do not add a second production executor.
- One GPU-resource owner and one semantic contract for effects, depth, color
  and text. Specialized backends may differ algorithmically within declared
  tolerances.
- `require_gpu_native` fails closed; it never silently falls back to CPU.
  Software rendering is a declared backend and test oracle.
- Fixture PASS is not release certification: require clean-commit builds,
  same-SHA CI and human visual review.

### Starting evidence (reported in proposal; recheck before acting)

- C3D camera closure and environment-light/IBL tickets are PARTIAL. Alpha-edge
  DoF has local PASS on 2026-11-03; camera C6, C7 human review, CI and final
  native scene remain. Software diffuse/specular and HDRI producer exist;
  strict-native Vulkan cubemap sampling remains incomplete.
- C3D text quality ticket is open: whole-run HQ shadow, glyph/stroke
  representation and editorial corpus remain.
- CM3D animated glTF and HDRI producer tickets are closed producer-side;
  same-SHA CI remains a release gate. The 2026-11-03 showcase records animated
  mesh collapse under camera rotation, 2D image interference with mesh, and
  incomplete software mesh occlusion.
- Template `WebTextLayout.cpp` uses shaped HarfBuzz measurements/clusters;
  `WebHeadlineHighlightPack.cpp` and `WebPageHighlightPack.cpp` still estimate
  width on some paths. Reuse the canonical shaped layout; add no fourth layout.
- C3D `inline_mesh.comp` loops over every triangle for every covered mesh
  pixel. Keep only where measurements justify its scale.

### P0 — Correctness and release blockers

| ID | Repo | Atomic action | Required gate/evidence |
|---|---|---|---|
| P0-01 | All | Freeze scope; record C3D/CM3D/Template SHAs, lockfiles, GPU and toolchain | Version manifest and reproducible build |
| P0-02 | C3D | Green main CI for build, tests and SDK without deleting gates | Exact current C3D app/include/src worktree mirror built successfully on the GPU host: release CLI, CUDA/Vulkan/NVRTC ON, 683/683 steps with `-j12`, exit 0; binary SHA-256 `f22f9b5e1d3cc02bf4deea5c7c89ca9f3aff6a2a85ca5e7c639b7b61ed679a94`. Dependency manifest mode was OFF and the installed release vcpkg tree was reused. Clean-checkout reproducibility, tests/SDK, vcpkg manifest install and candidate-SHA CI remain open. [Build record](evidence/chronon-2.0-baseline-20261103/README.md) |
| P0-03 | CM3D/Template | Align unit/integration CI to consuming C3D versions | CM3D adapter + 386 producer checks PASS against current C3D headers locally; declared version matrix and same-SHA CI remain open |
| P0-04 | C3D/CM3D | Isolate camera yaw/pitch and `inline_mesh.animation` defect | **Local fixture PASS (2026-11-04):** camera orbit + pitch/yaw and a mixed skin/morph glTF primitive over 0–149, software + Vulkan, no empty/collapsed frame, bounded image coverage. [Evidence](../ChrononMotion3d/docs/evidence/camera-mesh-yaw-isolation-20261104/README.md). Not release certification; P0-02 same-SHA CI and P1-16 review remain open. |
| P0-05 | C3D | Fix 2D image layer behind 3D mesh interference | **Local typed fixture PASS (2026-11-04):** image and `plane3d` software/Vulkan raw outputs match byte-for-byte after aligning Vulkan's depth epsilon with CPU. [Evidence](evidence/image-mesh-occlusion-20261103/README.md). Original showcase, strict-native video, same-SHA CI and human review remain release gates. |
| P0-06 | C3D | Unify camera-space depth for plane, mesh, text and image | **Local mesh/image permutation fixture PASS, reconfirmed on current Vulkan/software build (2026-11-04):** both authoring orders byte-identical across backends. [Evidence](evidence/image-mesh-occlusion-20261103/README.md). Full plane/mesh/text/image permutations and release gates remain open. |
| P0-07 | C3D | Separate alpha-test/cutout from blending; declare order and Z writes | Vulkan alpha-cutoff/source-over primitive passes 23 assertions and matte 25 assertions on A4000; **partial only**. Two-mesh/two-plane goldens and full material/occlusion coverage remain open. [Evidence](evidence/image-mesh-occlusion-20261103/p007-alpha-depth-tests-20261104.log) |
| P0-08 | C3D | Unify pixel-truth for raw/video and still/PNG | **PARTIAL:** raw→PNG verbatim extractor and video-lane pixel truth are established; `render -o still.png` remains explicitly non-canonical and has historical text/frame/backend parity failures. [Evidence](evidence/pre-encode-pixel-truth-2026-09-17.md). A same-commit fixture proving still/video parity with visible text+mesh is still open. |
| P0-09 | C3D | Fix Vulkan frame leases, semaphores, descriptors and surface reuse lifetime | Five focused A4000/Vulkan cases pass 181 assertions; same surface handle also survives 100 async upload/readback/release cycles (702 assertions), validation enabled. **Partial only:** complete render jobs, cancel/reset/teardown and release CI remain open. [100-cycle evidence](evidence/image-mesh-occlusion-20261103/p009-100-cycles-20261104.log), [lifecycle evidence](evidence/image-mesh-occlusion-20261103/p009-lifecycle-tests-20261104.log) |
| P0-10 | C3D | Complete camera DoF C6 150f and C7 human review/same-SHA | **C6 local run PASS (2026-11-04):** DoF/shutter and sharp OFF each completed 150/150 raw RGBA8 frames; frozen rack-focus thresholds pass. Current-source strict-native Vulkan/NVENC DoF/shutter also completed 150/150 frames, decodes cleanly, has monotonic PTS, zero black intervals and zero validation/VUID matches. Overall ticket stays **PARTIAL** pending C7 human review and same-SHA CI. [Native report](evidence/camera-closure-final/c6-v3-native-verification.json), [raw metrics](evidence/camera-closure-final/c6-v3-raw-focus-report.json) |
| P0-11 | C3D/CM3D | One immutable placement from resolved camera/layer sample | Identical matrices/bounds at build, refresh and render; no double transform |
| P0-12 | C3D | Crash/UAF, memory-budget and concurrent-job fail-closed coverage | CPU sanitizers, GPU stress, zero crashes and lifecycle metrics |
| P0-13 | C3D | Full native Vulkan/NVENC video without black frames or undeclared fallback | **Local acceptance PASS (2026-11-04):** 150-frame A4000 run on current-source release CLI with Vulkan `require_gpu_native`, native NVENC, validation enabled, no single-flight; 1920×1080/30 H.264, 150 decoded frames, PTS 0–149, decode clean, zero blackdetect intervals and zero validation/VUID matches. Evidence and hashes: [verification report](evidence/camera-closure-final/c6-v3-native-verification.json). Release gate still depends on clean same-SHA CI and candidate build provenance (P0-01/02). |
| P0-14 | All | Malformed glTF/RenderPlan/assets and resource-limit gates | No uncontrolled OOM; typed errors and fuzz corpus |

P0-01 evidence snapshot: [`chronon-2.0-baseline-20261103`](evidence/chronon-2.0-baseline-20261103/README.md).
It records observed revisions, dependency pins, toolchain and remote GPU state;
it is explicitly **PARTIAL**, since the worktrees are dirty and a clean
reproducible build has not yet been demonstrated.

### P1 — Visual quality and declared feature set

| ID | Repo | Action | Required gate/evidence |
|---|---|---|---|
| P1-01 | Template/C3D | Replace editorial preset `estimateWidth` with canonical shaped layout | Highlight on/off preserves every glyph position |
| P1-02 | Template/C3D | Span range geometry with clusters, line breaks, bidi, UTF-8, advances and baseline | One highlight band per line; no glued words or relayout |
| P1-03 | Template | Complete ArticleHighlighter, DarkQuote, ArticleSpotlight, EvidenceCard | Four faithful 1080p videos/goldens and visual review |
| P1-04 | C3D | Whole-run text silhouette shadow | No clipped/truncated glow or multiplied glyph shadows |
| P1-05 | C3D | Fast/HQ text with gamma, AA, MTSDF and linear color | Font/zoom/raw corpus, screenshots and approved metrics |
| P1-06 | C3D | Assign blur/bloom to correct layers; keep foreground text sharp | Neon preset with background DoF; readable text, no bleeding |
| P1-07 | C3D | Complete Vulkan strict-native specular IBL cubemap | HDRI toggles, roughness mips, software/GPU parity |
| P1-08 | C3D | PBR parity for supported software/Vulkan subset | Metal/plastic/gem goldens, base and metal-rough textures, alpha |
| P1-09 | C3D | Finalize production shadow catcher and native shadows | Correct ground alpha, pixel validation and human review |
| P1-10 | CM3D/C3D | E2E `CameraSequence`/active cut and subframe focus/shutter | No implicit crossfade; out-of-order seek and equivalent frames |
| P1-11 | CM3D/C3D | Combined focal length/sensor/lens shift/near/far/DoF camera tests | Numeric projection goldens and edge-case video |
| P1-12 | C3D | Bounded glTF support on realistic models | Explicit supported contract and errors for excluded extensions |
| P1-13 | Template | FloatingNeonText+ProductPackshot preset | 5s golden, alpha subject, four distinct-Z products, parallax, light focus and bloom |
| P1-14 | Template | Mixed PNG 2.5D/glTF preset | Equivalent A/B scenes and cost benchmark |
| P1-15 | C3D | Linear/premultiplied color, sRGB textures and NV12 regression | Color canary goldens and ΔE/MAE separate from encoder artifacts |
| P1-16 | All | Final human approvals for text, DoF, occlusion, glow and shadows | Reviewer checklist and approved reference artifacts |

### P2 — Measured performance

| ID | Repo | Action | Required gate/evidence |
|---|---|---|---|
| P2-01 | C3D | Profile `inline_mesh.comp` at 10/100/1k/10k triangles | GPU ms/throughput graph and justified choice |
| P2-02 | C3D | Add Vulkan graphics raster path for real meshes; retain compute only if useful | Same depth/alpha/compositing; measured A/B threshold |
| P2-03 | C3D | Reduce `VulkanBackend::Impl` using existing authorities | No parallel ownership; lifecycle and performance green |
| P2-04 | C3D | Split compilation into typed artifact phases, retain `CompiledProgram` | Equivalent output; no hot-path rebuild |
| P2-05 | C3D | Separate versioned content/placement/temporal/device cache keys | Field-by-field mutation tests; no stale/ghost hits |
| P2-06 | C3D | Prepare assets/topology once per job; update per-frame data only | Measure CPU build/frame and allocations/frame |
| P2-07 | C3D | Async Vulkan→CUDA→NVENC pipeline with real wait/encode telemetry | No strict-native readback; frame hash/repeat and backpressure recorded |
| P2-08 | C3D | VRAM/job-class admission and backpressure without spin loops | Two jobs cannot illegally contend; bounded VRAM |
| P2-09 | C3D | Reduce compositing passes only with profiler evidence | 1080p/4K benchmark, pixel goldens, no regressions |
| P2-10 | C3D | Fixed static overlay/editorial/2.5D/glTF/shutter-DoF benchmarks | P50/P95, GPU ms, wall time, VRAM, encode time, warm/cold |
| P2-11 | All | Binary/dependency/clean-build/hot-allocation budgets | Lite/full profiles and release-to-release report |

### P3 — Release, interoperability and support

| ID | Repo | Action | Required gate/evidence |
|---|---|---|---|
| P3-01 | Template/RenderingGen | Optional OpenTimelineIO editorial import (clips, cuts, ranges, media refs) | `.otio`→Chronon scene→MP4; explicit errors for unsupported effects |
| P3-02 | C3D/Template | Census and migrate legacy `CompiledComposition` | No consumers or optional adapter; C22 corpus 8/8 |
| P3-03 | C3D | Remove duplicate Fill/compat models only after ABI decision | Architecture exception ratchet and visual tests |
| P3-04 | C3D | Installable SDK (`find_package`), release CLI and external consumer | Clean Linux/Windows CI install; 5s example render |
| P3-05 | CM3D | Reproducible CM3D→RenderPlan bundle without renderer dependency | ABI and dependency-boundary tests |
| P3-06 | Template | Versioned catalogue, preset schema, asset manifests and samples | Ten preset goldens with font/asset/license provenance |
| P3-07 | RenderingGen | Job planner/receipt with input hash, preset version, device, route and output hash | Idempotence, retry/cancel, clear metrics/errors |
| P3-08 | C3D | Audio mux/sync and captions if included | Start/end sync, seeks and valid MP4 |
| P3-09 | All | SBOM/licenses for dependencies/assets, security policy and support matrix | Compliance check and release notes |
| P3-10 | All | Lite headless CPU and full GPU/native-video release profiles | Install, smoke test, compatibility docs |
| P3-11 | All | `v2.0.0-rc1`, freeze, final acceptance and tag | Zero P0; declared P1; green CI; reviewed video goldens; binary links |

### Required acceptance corpus and threshold policy

- Four editorial scenes: yellow title, dark quote, paper spotlight and card
  over blurred footage; SVG/text highlights over Unicode, ligatures and multiple
  lines; raw and video output.
- Neon 2.5D packshot: alpha subject, four PNG products, lens, dolly/orbit
  camera, neon text, glow, selective blur and correct occlusion.
- Mixed 3D: one animated skinned+morph glTF, opaque mesh, alpha-cutout mesh,
  `plane3d`, 2D image behind it, rotating/dolly camera.
- Optical three-depth: 150 frames at 1080p/30, rack focus, shutter, fractional
  alpha and near/far edges; zero black frames and raw parity.
- Performance: static 2D, fast video, 2.5D, mixed glTF and DoF/shutter at
  1080p/4K, warm/cold, one/two jobs, with differentiated budgets.

For Optical Studio, use only the frozen thresholds in
`TICKET-CAMERA-DEFINITIVE-CLOSURE` (e.g. MAE ≤ 2, p99 ≤ 10 for the relevant
raw comparison). Do not invent a universal threshold for PBR, lossy encoding,
text or new fixtures: define each fixture and metric first.

### Integration policy

| Candidate | Proposal |
|---|---|
| Filament | Study as BRDF/IBL/camera/post-FX reference; do not embed as an engine |
| Vello | Study sparse strips/vector compositing; no Rust runtime in v2 |
| Blend2D | Keep existing software oracle; do not duplicate it |
| msdfgen | Targeted optional spike only if current generator fails the corpus |
| HarfBuzz | Reuse existing shaping, clusters and caret geometry via `TextRunLayout` |
| cgltf/fastgltf | Choose one only if CM3D reader limits realistic production models; test first |
| meshoptimizer | Optional offline preprocessing after real-mesh benchmark |
| VMA | Adopt only if it resolves memory authority after audit |
| Tracy/RenderDoc | Dev-only profiling/capture; no duplicate runtime telemetry |
| OpenTimelineIO | Optional editorial adapter, not a full editor |
| OpenColorIO | Use existing C3D integration; do not create a second one |
| glTF-Validator/Sample-Assets | Test/dev only, with asset licenses respected |
| rlottie | Optional plugin only if existing Lottie support is insufficient |

Do not add O3DE/OSG/osgEarth, another general compositor, camera rig, text
layout or ray tracer to v2. GPL Natron/Friction may be studied; do not copy
code into the permissive core without license review.

### Merge order, stop rule and v2.0 exit

1. Freeze SHA/CI baselines (P0-01..03).
2. Fix camera/mesh, depth, alpha and still-output truth (P0-04..08).
3. GPU lifetime, C6/C7, strict native and validation (P0-09..14).
4. Editorial text and four presets (P1-01..06).
5. 3D IBL/PBR/shadows/camera cuts/lens (P1-07..12).
6. Neon golden scene and human reviews (P1-13..16).
7. Profile and optimize without degrading goldens (P2).
8. Interop, legacy and release packaging (P3).

Stop a merge if pixels change without an intentional visual-spec change, a
parallel state path is introduced, a domain golden fails, or performance
exceeds its approved budget. Move live gates from historical tickets into the
single canonical tracker before treating those tickets as historical sources.

Chronon 2.0 closes only when all P0 pass; in-scope P1 pass with goldens and
reviewers; P2 benchmarks and no-regression are signed; P3 installability,
documentation, licensing and CI pass; unsupported capabilities fail
explicitly. Desirable non-blockers move to v2.1 and do not enter v2.0
acceptance.

### GPU host reference

The documented remote host is the SSH alias `gpu-node` (NVIDIA RTX A4000,
16,376 MiB), with workflow in
[`REMOTE_GPU_WORKFLOW.md`](reference/REMOTE_GPU_WORKFLOW.md). Check occupancy
and selected Vulkan ICD before a run; do not stop other workloads. A P0-05
post-fix diagnostic was run on `gpu-node` on 2026-11-03 using the Vulkan ICD
and the scoped one-frame fixtures. It rendered through Vulkan with host raw
readback (`gpu-hot-path-mode auto`); it did not certify strict-native NVENC.
The measured evidence is in
[`image-mesh-occlusion-20261103`](evidence/image-mesh-occlusion-20261103/README.md).
The same fixture directory also records the P0-06 depth-order failure and
post-fix pass for two 128×96 image/mesh layer orders, including raw software
and Vulkan hashes. The final Vulkan readback pair is identical to software;
GPU matte and native-occlusion registry tests pass on the A4000. This remains
a minimized fixture, not strict-native video or the full Mixed 3D acceptance
corpus, so P0-06 stays partial at release scope.
