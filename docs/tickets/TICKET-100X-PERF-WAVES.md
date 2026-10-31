# Chronon 100× Performance Waves — Ticket Specs (B1/B2/B3)

Status: `MIXED — B1 LAND; B2 IMPLEMENTED / GATE FAILED; B3 IMPLEMENTED / ACCEPTANCE PARTIAL` — this file is a ticket specification, not a blanket acceptance claim. The Linux NVML sampler now loads the correct library and reports actual NVENC samples in the sidecar, but the latest five-run B2 fixture remains below both the 300 render-loop fps and 90% utilization gates. MTSDF outline/shadow golden and VRAM gates remain unverified. No claim of 100× single-clip latency or 100 overlay/min follows from these component results alone.
Created: 2026-09-09
Baseline evidence: job_1788973104795535356_86264102 sidecar + TICKET-ZERO-COPY-ZWAVES.md F1–F7
Owner: Chronon rendering-core maintainers

Measured state after the 2026-09-09 E2E (19s clip, 456 frames, 1080p24):

| Metric | First E2E (Sep 9 AM) | This E2E | Target path |
|---|---|---|---|
| render_loop_ms | ~5100 | 2338 | ≤450 (100× = 0.19s wall) |
| render_loop_fps | ~89 | 195 | ≥2400 |
| render-only fps | — | 427 | ≥2400 |
| nvenc_utilization_avg | 72% | 73% | >90% (feeds faster) |
| gpu_surface_copy_frames (gate 6) | — | **0** (Z3 landed) | 0 |
| rgba_to_nv12_frames (gate 4) | — | **0** | 0 |
| encoder_staging_copy_bytes | — | **0** | 0 |
| cuda_host_upload steady | — | **0** | 0 |

The zero-copy Z-waves already landed in Chronon: no per-frame D2D copy, no
RGBA→NV12 pass, no host upload. The remaining gap to 100× single-clip latency
is NOT copy-bound; it is pass-structure and pipeline-depth bound. The three
waves below are the reachable remainder, in expected-gain order.

---

## TODO — cosa manca

- [ ] **Gate B2 — FAIL:** the latest five-run local NVML-fix measurement is documented below; neither FPS nor canonical utilization reaches the gate.
- [ ] **Gate B2 — FAIL (precedente rerun 2026-10-30):** fixture title-drift di 456 frame, strict Vulkan/NVENC, binario finale SHA-256 `67b8c1d5…`. Run canonico: encoded 456/456, exit 0, `render_loop_fps` **238.200**, end-to-end 139.284 fps, drain 12 µs/job; ancora **20.60%** sotto il target 300. Sidecar `nvenc_utilization_avg` nullo; polling `nvidia-smi` sui campioni encoder attivi 95.43%, metrica non canonica. Output SHA `2520fabe…`. Varianti 16 thread (238.50), depth 12 (235.81), depth 6 (233.57) e 16 surface NVENC (236.30) dentro la banda di varianza 232–239: modifiche ripristinate. Risultati: [`b2-final-20261030-runs.json`](../evidence/100x-perf-waves-b2-20261029/b2-final-20261030-runs.json). Il gate resta aperto.
- [x] Instrumentare e pubblicare il breakdown GPU per-pass (query Vulkan TOP/BOTTOM, attribuzione per `GpuPassKind`, sidecar e counter backend): vedi la sezione 2026-10-30 in coda.
- [x] Implementare il percorso GPU MTSDF (native MTSDF preparation/raster path exists; focused tests pass).
- [ ] **Certificare B3:** manca equivalenza visiva outline/shadow vs Coverage e budget ≤256 MB/font set, oltre al gate `text_pass_draws_per_frame == 1`. La correzione locale dell'inizializzazione R8 Coverage e il test mirato sulle pagine Coverage/MTSDF sono documentati sotto; non sostituiscono questi gate.
- [ ] Golden canonico 100-item e throughput 100 overlay/min non eseguiti; nessun claim 100×.

## B1 — Persistent overlay layer for static text/watermark (≈1.4–1.6×)

**Status:** `LAND (DirectYUV)` — `DirectYuvProgram` caches the prepared `DirectYuvTemplate` across static cue intervals $[cue\_from, cue\_until)$, skipping `composition_->evaluate(context)` and per-frame quad reconstruction entirely (`template_evals ≈ number_of_cues`, `template_reuses = frames - cues`).

**Problem.** Burned subtitles + watermark are authored as per-cue text layers.
Chronon re-rasterizes and re-composites glyphs on every frame they span even
though their pixels change only at cue boundaries. In the E2E, cue layers span
~19s of a 19s clip → ~455 identical raster+composite passes.

**Change.**
1. In the graph builder (`src/compiler/graph_builder/graph_builder_layer_pipeline.cpp`),
   detect text/image layers whose Style, Text, Animation==nil and
   StartFrame<=first_frame && EndFrame>=last_frame are *static for the job*.
2. Promote them to a `PersistentOverlayNode`: rasterize once at prepare time
   into a premultiplied-alpha surface (job-lifetime, `LifetimeClass::JobPersistent`,
   same protocol as the glyph atlas pages).
3. Per frame, the node emits a single alpha-blend draw of the cached surface
   (one quad, no text shader, no glyph lookups).
4. Invalidation key: hash(style_id, text, font, size, position). Cue changes
   allocate a new overlay surface; old ones are recycled by the slot pool.

**Gate (sidecar).** New counter `persistent_overlay_frames` vs
`text_raster_frames`; after landing, `text_raster_frames ≈ number_of_cues`,
not frames. `render_loop_ms` regression test: 456-frame static-text clip must
not exceed 1.2× the no-text control. This gate is distinct from P1's 3 s vs
30 s throughput ratio and pixel-parity fixture; do not mark it passed from P1.

**Risk.** Interaction with `TextAnimators`/word-reveal presets — those keep
the per-frame path (they are genuinely animated).

---

## B2 — Deep frames-in-flight in the graph executor (≈1.3–1.5×)

**Status (25 Oct 2026):** implementation exists in the Vulkan ring configuration (`kSubmissionRingDepth = 6`), but this ticket's sidecar acceptance gate (drain wait ≤50µs/frame, NVENC ≥90%, render_loop ≥300 fps on its specified workload) is NOT certified here. Do not infer those metrics from the independent P1 live run.

**Problem.** `vulkan_backend_submission_private.cpp` waits per batch-slot
fence before re-recording; `frame_batch_drain_wait_us` is exported per frame.
At 195 fps the CPU/GPU pipeline has ≤2 frames of depth; NVENC sits at 73%
average utilization purely because it is starved between batch drains.

**Change.**
1. Raise the submission ring to `N = kCudaEncodeRingSlots` (6) independent
   command batches, each with its own fence + semaphore pair; record frame k+2
   while frame k is still on the GPU.
2. Replace the per-slot `vkWaitForFences` (maximum timeout) with a free-slot
   check that records only when a slot is *actually* free (the ring already
   has this shape; the loop currently blocks before recording).
3. Backpressure: when all 6 slots are busy, the writer's bounded queue applies
   natural backpressure — no busy-wait.

**Gate (sidecar).** `frame_batch_drain_wait_us_per_frame` ≤ 50µs; NVENC
utilization avg ≥ 90% on the static-text clip; `render_loop_fps` ≥ 300 on the
E2E workload.

**Risk.** Order-sensitive surfaces shared across batches (framebuffer clears)
need per-slot sync already provided by `InteropFrameState`; the wave-12
`GpuSlotPool` lease model is the correct vehicle (one lease = one in-flight
batch).

---

## B3 — Single-pass MTSDF text rendering (≈1.1–1.2× after B1; enables animated text at scale)

**Local atlas correction:** R8 Coverage pages previously used a float per pixel
for their initial full-surface upload, which made the upload four times larger
than the surface contract and caused allocation to fail. The initial upload now
uses the native byte size for both R8 and RGBA8 pages. On the local dirty tree,
`cmake --build build/chronon/linux-video-test --target
chronon3d_backend_registry_tests -j12` passed, followed by the focused test
`VRAM glyph atlas initializes coverage and MTSDF pages at their native sizes`:
1 case, 16 assertions, Vulkan device NVIDIA RTX A4000. It covers page creation,
formats, residency and reuse, not rendered pixel parity or the B3 memory and
draw-count acceptance gates. The Release CLI was also rebuilt with `-j12`.

**Status (27 Oct 2026):** IMPLEMENTED / ACCEPTANCE PARTIAL. Chronon has a native MTSDF preparation and raster path (`gpu_text_run_mtsdf.cpp` and the MTSDF tile raster shader). Focused generator, font identity, and GPU raster canary tests pass. This does not establish the prior ticket's full text-run pass-count, outline/shadow golden equivalence, or font-set VRAM budget; the P1 static-title result does not exercise B3's animated-text path.

**Problem.** The glyph atlas already stores `GlyphRepresentation::Mtsdf`
(RGBA8 SDF pages, `gpu_glyph_atlas.hpp`) but the runtime feeds it CPU coverage
bitmaps via `acquire()` → `upload_surface_region` per glyph
(`gpu_glyph_atlas_core_detail.hpp` ~line 226). Outline+shadow are then produced
by separate stroke/shadow passes (ShadowNode translate+blur,
`graph_builder_lighting_passes.cpp:95`).

**Change (per ADR-009 ticket #12, msdfgen as opt-in `text-msdf` extension).**
1. Generate MTSDF atlases OFFLINE at font-preparation time (build-time tool,
   not per-job); ship atlas binaries as job assets.
2. Fragment shader samples the 4-channel SDF once: glyph fill from the alpha
   channel, outline via distance offset, soft shadow via a second sample at
   (offset_x, offset_y) with smoothstep — 1 draw call per text run instead of
   fill+stroke+shadow passes.
3. Keep the Coverage lane as fallback for non-MSDF fonts (capability matrix in
   the `VideoCompositor` resolver, no new resolver).

**Gate (NOT RUN in this status update).** `text_pass_draws_per_frame` == 1
for MSDF runs; visual regression golden for outline/shadow equivalence within
tolerance; atlas VRAM budget ≤ 256MB per font set (4 pages × 4 representations
max).

**Note.** After B1, per-glyph cost is mostly off the hot path; B3's remaining
value is animated/kinetic text and higher-quality outlines, plus shrinking
VRAM (one MTSDF page vs coverage pages per size).

---

## Order of execution

1. **B2** (executor depth) — no visual semantics change; implementation exists,
   but verify its sidecar gates on the specified workload before claiming a gain.
2. **B1** (persistent overlay) — biggest single-clip win; implementation LAND,
   but keep the P1 live certification scoped to its dedicated preset/backend.
3. **B3** (MTSDF) — last; SPEC until its draw-count, visual golden and VRAM
   gates pass; partially subsumed by B1 for static text.

## What 100× means after these

Single-clip latency lands ≈ 10–15× (≈1.3–1.9s wall for the 19s clip) — the
remaining cost is NVENC submission latency + mandatory per-frame composite of
the *changing* video layer, which cannot go below ~2ms/frame at 1080p on this
class of GPU. **100× throughput is then achieved by concurrency**: with B1+B2
the steady-state per-clip VRAM drops (fewer text passes, tighter atlas) so
4–6 clips render concurrently per GPU → 10-language batch in <30s of GPU
time. VRAM budget work (`GpuMemoryBudget`, atlas `set_budget_bytes`) is the
enabler and needs its own small ticket.

## Verification checkpoint — 27 Oct 2026

The fast-dev Vulkan build produced the CLI; the three focused B3 tests pass on the current local tree:
`chronon3d_text_tile_raster_canary_tests`,
`chronon3d_mtsdf_generator_tests`, and
`chronon3d_mtsdf_font_identity_tests` (3/3). Final serial rerun on 29 Oct: raster canary PASS (0.90 s), MTSDF generator PASS (2.45 s), font identity PASS (0.10 s). The first link attempt hit a stale 1.58 GB mold temporary left by the failed linker; after removing that temporary, all three CTest gates passed. The saved benchmark report is
`docs/tickets/evidence/100x-perf-waves-b2-20261027.json`.

On `chronon_text_full_static` for 19 seconds, the saturation benchmark rendered
1,383 frames at **72.77 fps**; `frame_batch_drain_wait_us_per_frame` was
**653.90 µs**, above the ≤50 µs gate, and therefore the render-loop rate was
also below the ticket's ≥300 fps threshold. This benchmark did not run an
encoder, so NVENC utilization is **not measured**. B2 is not accepted by this
run. It is a diagnostic run rather than the required encoder-backed 456-frame
acceptance workload.

The P4 live parity test `TestSegmentedSessionMatchesIndependentSingleItemRenders`
passed with the freshly built fast-dev Chronon binary. A separate attempt to run
the full overlay golden on 27 Oct stopped before rendering: the canonical
payload hit a TTS zero-duration/circuit-breaker failure. A no-audio retry after
updating the Semantic Engine completed script generation but produced no
`overlay_plan` and rendered zero items, so it is not a throughput sample. The
100 overlay/min gate remains open until the canonical 100-item golden is
rendered and validated on a healthy three-daemon pool.


### Session workload — 2026-10-29

[`static-text-456f.plan.json`](../evidence/100x-perf-waves-b2-20261029/static-text-456f.plan.json) is a deterministic 1920×1080/24 fps, 456-frame Inter Bold static-title fixture. The all-static version let duplicate-frame reuse bypass 455 encodes. The current probe adds a slow 20 px title drift so every frame is distinct while the words and styling stay fixed. Five strict Vulkan/NVENC runs all completed 456/456 encodes at 230.469–232.771 render-loop fps; mean drain wait was 19.8 µs/job; active-sample `nvidia-smi` NVENC utilization averaged 87.27%. B2 remains below 300 fps, and its native utilization sidecar remains null. This is session fixture evidence, not the historical production composition.

For T05/C2, three non-environment particle plans at 1080p/one frame were rendered strict Vulkan/NVENC MP4. End-to-end wall times: 1k=2.046524 s, 10k=2.607469 s, 100k=7.998100 s, including startup and encoder/mux close. These are fixture measurements, not kernel times or before/after speedups; decoded RGB hashes and logs are in `docs/evidence/lights-shadows/T05-native-vulkan-20261029/c2-results.json`.


### Final B2 rerun — 2026-10-29

Five sequential strict Vulkan/NVENC runs of the moving-title 456-frame fixture
all exited 0 and encoded 456/456 frames. Render-loop FPS was 230.797, 231.939,
232.999, 232.752 and 231.654 (mean 232.028); end-to-end FPS mean was 131.616.
Mean frame-batch drain wait was 11.4 µs/frame. `nvidia-smi` active-sample
encoder utilization averaged 96.62% (83.11–100% by run), while the canonical
NVENC sidecar field remained null. All five output hashes matched
(`2115ab511cda1da89e6a51ca6e3832e8934f89b9e437bddb2bd302ca59de51ad`).
Compared with the prior final Release mean 243.226 fps, this is −4.60%; compared
with Debug 231.506 fps, +0.23%. The 300 fps threshold is still missed by
22.66%, so B2 remains **NOT ACCEPTED**. Results: `docs/evidence/100x-perf-waves-b2-20261029/b2-final-20261029-runs.json`.

### Post-diagnostic final rerun — 2026-10-29

After the T05 investigation, the same five-run fixture was repeated as a final
measurement. All runs exited 0 and encoded 456/456 frames with a stable output
hash (`2115ab511cda1da89e6a51ca6e3832e8934f89b9e437bddb2bd302ca59de51ad`).
Render-loop FPS was 228.754, 230.255, 230.968, 231.612 and 232.716, mean
230.861; this is 0.50% below the prior 232.028 mean, so there is no measured
improvement. Mean end-to-end FPS was 125.332, drain wait 14.2 µs/job, and
active-sample `nvidia-smi` encoder utilization was 100% in each run; canonical
NVENC utilization remained null. The 300 fps gate still fails. Evidence:
`docs/evidence/100x-perf-waves-b2-20261029/b2-post-diagnostic-20261029-runs.json`.

### Current-tree Release final rerun — 2026-10-29

After the latest T05 Release acceptance and focused Lighting tests, the B2
fixture was run five times using the rebuilt `linux-video-release` CLI. Every
run encoded 456/456 frames and produced the same output SHA. Mean render-loop
rate was 238.703 fps (206.280–252.653), which is +3.40% over the previous
230.861 fps mean and remains 20.43% below the 300 fps gate. Mean end-to-end
rate was 150.447 fps; mean frame-batch drain was 12.8 µs/job. Active-sample
`nvidia-smi` encoder utilization averaged 98.48%, while the timing sidecar
field remains null. The result is a measured improvement in this fixture but
B2 remains **NOT ACCEPTED**. Evidence:
`docs/evidence/100x-perf-waves-b2-20261029/b2-final-current-release-20261029-runs.json`.

### Final-binary rerun — 2026-10-30

The source-identical Release CLI (`SHA-256 67b8c1d5…`) was rerun on the same
456-frame fixture with the canonical `run_b2_benchmark.py` harness. It exited 0
and encoded 456/456 frames at **238.200 render-loop fps** (end-to-end 139.284,
drain 12 µs/job, active-sample `nvidia-smi` 95.43%, canonical sidecar null;
output SHA `2520fabe…`). The gate is still 20.60% below 300 fps, so B2 remains
**NOT ACCEPTED**.

Targeted variants did not move the gate: 16 CPU render threads 238.50 fps,
12-slot depth 235.81, 6-slot depth 233.57, 16 NVENC surfaces 236.30 — all inside
the 232–239 fps run-to-run band. The depth and surface changes were reverted;
the final binary is the baseline depth-4 / 8-surface configuration. The remaining
cost is the ~2.5 ms `prepare`/`avcodec_send_frame` serialization, not queue
depth. Evidence:
`docs/evidence/100x-perf-waves-b2-20261029/b2-final-20261030-runs.json`.

### Per-pass GPU breakdown and CPU profile — 2026-10-30

The render loop's GPU time now has a per-pass-kind breakdown from Vulkan
timestamp queries. On the canonical 456-frame fixture: `composite` **173.889
ms** (73.4%, 7 regions/frame, median 0.3797 ms, p95 0.4133) and `layer_batch`
**63.081 ms** (26.6%, 1 region/frame, median 0.1337 ms, p95 0.1676). The two
kinds cover **99.2%** of the 238.985 ms device span (TOP→BOTTOM of each
submission). GPU work is therefore **0.51 ms of a 4.25 ms frame**: the gate is
CPU-side, as suspected.

A `perf record -F 999 -g` profile of the same run found **no application
symbol above 0.3%** self time and **25.3% of samples in `__sched_yield`** from
idle TBB workers (`thread_dispatcher::process`). Shrinking the TBB arena by
pinning cores moved the render loop only from 238.3 to **245.8 fps** (8 cores,
+3.2%; 4 cores 242.3; 2 cores 239.9), still at least 18% below 300.

**No optimization was adopted.** The only measured lever cannot close the
gate; reaching 300 fps needs the paradigm changes (persistent engine,
decoupled encode), not a micro-fix. **B2 remains NOT ACCEPTED.**

Evidence: [`docs/evidence/gpu-pass-timestamps-20261030/README.md`](../evidence/gpu-pass-timestamps-20261030/README.md),
`b2-pass-timestamps-c-results.json` and the matching `*.timing.json` sidecar.

### Linux NVML sampler correction — local worktree

The sampler previously attempted to load `nvml.dll` on Linux, leaving the
canonical hardware utilization fields null. It now loads `libnvidia-ml.so.1`;
encoder and decoder utilization are reported only when their respective NVML
queries succeed. The sampler averages all successful samples during its job
window, including idle samples; this is not the same metric as the separate
`nvidia-smi` nonzero-sample average. NVML device-level data can also include
other workloads; the sampler currently selects device index 0.

The Release CLI built with `cmake --build build/chronon/linux-video-release
--target chronon3d_cli -j12`; the focused NVML sampler lifecycle test passed
3 cases / 15 assertions. Five runs of `run_b2_benchmark.py` on RTX A4000 with
the local modified tree encoded 456/456 frames each, with stable output SHA
`2520fabea7d548ee04b6f43bd747f9ad64cf1fc7aef1818d01a9f83cb1f7120c`.
Render-loop FPS: 239.141, 243.896, 241.616, 232.683, 222.497; mean **235.967**.
Canonical NVENC utilization: 76.0, 62.5, 77.778, 68.222, 63.556%; mean
**69.611%**. Mean drain counter: 11.6 µs/job. Both FPS and utilization remain
below their respective B2 thresholds. No performance gain is claimed; the
benchmark uses the dirty local tree and is not same-SHA certification.

Results: `docs/evidence/100x-perf-waves-b2-20261029/b2-nvml-fixed-runs.json`.
The harness overwrites the MP4, log and timing sidecar per run; only the final
run's individual artifacts remain, while the JSON retains all five summaries.
