# Chronon 100× Performance Waves — Ticket Specs (B1/B2/B3)

Status: `MIXED — B1 LAND; B2 IMPLEMENTED / GATE NOT CERTIFIED; B3 SPEC (25 Oct 2026)` — this file is a ticket specification, not a blanket implementation status. The B1 DirectYUV static-template path is landed in source and the broader P1 static preset has one live PASS recorded in PipelineGen; B2 submission-depth and B3 MTSDF gates below are not certified by that P1 run. No claim of 100× single-clip latency or 100 overlay/min follows from B1 alone.
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

## TODO — cosa manca (agg. 2026-10-27)

- [ ] Certificare il gate B2 sul workload specificato dalla scheda: drain wait ≤50µs/frame, NVENC ≥90%, render_loop ≥300 fps.
- [ ] Implementare B3 (MTSDF) secondo la spec di questa scheda.
- [ ] Nessun claim “100×” o “100 overlay/min” finché resta solo il B1 land.

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

**Status (25 Oct 2026):** SPEC / NOT CERTIFIED. No evidence in this ticket establishes the one-draw-per-frame gate, outline/shadow golden equivalence, or the font-set VRAM budget; the P1 static-title result does not exercise B3's animated-text path.

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
