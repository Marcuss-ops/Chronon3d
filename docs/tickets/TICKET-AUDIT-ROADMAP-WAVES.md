# TICKET-AUDIT-ROADMAP-WAVES — Audit roadmap closeout (Waves 1–6)

Status: IN PROGRESS (code landed; build/CI verification pending)
Owner: working session (repo owner + agent)
Created: 2026-09-05
Baseline: `main@2a3ee8d4e98a17c6ec365e4db6b07bd834397420`

## Purpose

Executes the architecture audit roadmap produced against
`main@077a44275d7414ac66912a0e81fb53ed205ac3b4` (video execution authority,
telemetry zombie closeout, surface-lifetime certification, NV12 CUDA
authority, composite ownership metrics, easy cleanup).  Only code/delta
entries live here; canonical state is reported in
`docs/CURRENT_STATUS.md` / `docs/FOLLOWUP_TICKETS.md`.

## Wave 6 — Easy cleanup (code DONE)

1. **`src/sdk_impl_marker.cpp` DELETED.** The file was unreferenced by every
   CMake target (the MSVC `/MP` glob only sets compile options; the
   `chronon3d_sdk_impl_marker` OBJECT layer no longer exists) — zero
   CMake/script/doc references found before removal.
2. **Native stroke dead flattening demolished.** `try_native_path_stroke` in
   `src/backends/vulkan/vulkan_native_shape_dispatch.cpp` kept unreachable
   QuadraticTo (8-segment) / CubicTo (12-segment) / Close emission under the
   single-opaque-segment pre-scan gate (added in 4f4a217d).  The pre-scan now
   also captures the two endpoints and emission is a single `emit(move,
   line)`; a future dev cannot "re-enable" the approximate curve flattening
   by deleting the gate.
3. **Transient-cleanup scratch reuse.** `release_frame_transient_surfaces()`
   and `retire_completed_frame_transient_surfaces()` in
   `vulkan_surface_store_private.cpp` shared a per-call handle vector; both
   now reuse one `Impl::transient_release_scratch_` member (Impl methods are
   serialized on the backend instance).  Added explicit `<vector>` include.
4. **Grid-kernel scratch reuse — REJECTED with evidence.** Attempted
   `thread_local` reuse of `cols`/`active_x` in
   `render_grid_background_kernel()` produced a deterministic SIGSEGV
   (`chronon3d_sdk_tests`, GATE-10-PHASE-4-BLACK grid case): under nested
   oneTBB parallelism two kernel invocations can interleave their fill phases
   on one worker thread while another invocation's bodies are still reading,
   so any per-thread scratch is unsafe.  Reverted; the per-invocation
   vectors are the required concurrency-safe design.  Do not reintroduce
   thread-local scratch here without a per-invocation ownership scheme.
5. **Trace gates — verified wired, no change.** `tools/trace_gates/*.cpp` are
   built by the opt-in wrappers `tools/check_trace_{correctness,overhead,
   sql_report_selftest}.sh` and executed in CI Gate 8 (`trace-gates` job,
   `CHRONON3D_TRACE_GATES=1`, perfetto amalgamation fetched at CI time).
   They are deliberately outside the CMake source graph; registering them as
   a CMake feature would duplicate the opt-in wiring, not remove it.

## Wave 2 — Telemetry zombie closeout (code DONE)

The per-event sharded stores already have drains (`render_job_finalize.cpp`,
`pipe_export_finalize_telemetry.cpp` → `TelemetryRunSnapshot` →
`TelemetryManager::record_run` → SQLite) and warmup clears exist.  The
remaining zombie was **capture cost at Summary** (production default):

- New capture-level gate in `core/telemetry/render_telemetry.hpp`
  (`telemetry_capture_level()` / `set_telemetry_capture_level()`); the only
  writers are `TelemetryManager::configure()` / `set_level()` (single-writer
  config authority, forwarded from `telemetry_manager.cpp`).
- `emit_node_records()` (`telemetry_emitter.cpp`) now early-outs at Summary
  when per-node diagnostics logging is off: zero node name/kind/layer string
  construction, zero shard lock/push per node × frame.  Granular capture
  (node/layer/cache records) happens only at Detailed/Trace; the diagnostics
  log line remains available at any level.
- Culling emitter (`graph_builder_layer_pipeline_pass.cpp`) gated the same
  way (per-layer string + store cost at Summary eliminated).

## Wave 3 — Surface-lifetime certification (tests DONE)

Invariant/property suite appended to
`tests/backends/test_vulkan_surface_materializer.cpp`
(`VulkanSurfaceAuthorityInvariants`), all CPU-side with the existing fake
owner:

- planned transient cannot alias a persistent reservation **before first
  initialization** (regression for the 077a442 bug);
- `ensure()` never steals an uninitialized persistent reservation;
- released transient slot is deterministically reusable after prune;
- persistent reservation survives release and rebinds to the same slot
  without re-allocation;
- `ensure()` transient reuse prefers compatible **unused transient** slots
  only (never persistent);
- an initialized binding refuses to move to another physical slot.

## Wave 1 — Video execution authority (code DONE)

- `LegacyVideoExecutionRequest` / `resolve_legacy_video_execution()` /
  `detail/video_execution_legacy.hpp` DELETED (zero remaining callers).
- Public `VideoExecutionDecision` no longer carries the `path` /
  `render_fallback` migration view; the `VideoExecutionPath` enum is gone.
  The decision is the FINAL orthogonal `VideoExecutionPlan` + structured
  `reason` + optional `gop_plan`.
- `resolve_video_execution_with_gop_analysis()`: GOP pre-analysis is folded
  into the resolver so downgrades are decided before execution with a
  structured reason (`gop_copy_preflight_failed`, `gop_inspection_failed`,
  `gop_splice_unsafe`, `hybrid_pipeline_unavailable`, `gop_reencode_required`,
  `gop_all_copy_eligible`, `explicit_gop_copy`).
- CLI executor (`video_job_execute_dispatch.cpp`): one source preflight
  (`inspect_source_once`) → requirements/capabilities translation → FINAL
  plan → dispatch purely on the plan axes (PacketCopy both ways ⇒ whole-clip
  copy; composite == DirectYuv ⇒ DirectYuv render; else FullGraph).  All
  runtime "falling back to DirectYuv" / "hybrid pipeline TBD" executor
  decisions removed.  Receipt surface: `resolved_execution_reason` /
  `resolved_execution_valid` added to `FfmpegExportOptions`.
- Residual (documented): persisting the structured reason into the timing
  sidecar / SQLite telemetry tables is not wired yet — follow-up when the
  pipe timing sidecar gains a plan/reason record.

## Wave 5 — Composite ownership metrics (code DONE at baseline)

Census on HEAD shows the measurement and the redesign already landed:
composite cache bypass (`bypass_composite_for_zerocopy`, `cache_evaluator.cpp`)
keeps `use_count == 1` so the `reusable_inputs` / `reusable_bottom`
zero-copy path activates (`node_runner_single_node_detail.hpp`); counters
`composite_copy_pixels`, `composite_calls/pixels`, `alias_saved_bytes`,
`alias_reuse_count`, `framebuffer_reuses` exist and
`alias_saved_bytes`/`alias_reuse_count` reach SQLite.  No further change
made in this wave.

## Wave 4 — NV12 CUDA authority (unified in Wave 1D; GPU evidence pending)

The two NV12 implementations (`CudaNv12SurfaceCompositor` in backends/vulkan
vs `CudaDirectNv12Compositor` in media/video) with duplicated NVRTC/JIT
scaffolding were unified in Wave 1D (commit `f0cb4b6e`): the Vulkan-side
compositor TUs and the media-side direct compositor now live as ONE authority
— `media::video::CudaNv12Compositor` under `src/media/video/compositor/` —
consumed by the Vulkan frame importer and the native encoder.  A
`VideoCompositor` contract + registry/resolver facade was added as the
backend-neutral entry point.

The dual-lane equivalence baseline the audit originally ordered is no longer
measurable (only one lane exists).  The equivalence harness was therefore
rewritten as a determinism + contract matrix for the unified authority: every
corpus request runs through two INDEPENDENT compositor instances and must be
bit-exact (determinism), with documented per-case outcomes (foreground
composite / zero-contribution background passthrough / rejection of
NV12-kind overlay resources).  It is registered as
`chronon3d_nv12_compositor_contract_tests` (CUDA/NVRTC-gated) and runs with
`CHRONON3D_RUN_CUDA_TESTS=1`; GPU-host certification is still pending.  The
shared JIT/kernel authority and FullGraph + DirectYuv adapters remain the
follow-up once a GPU-host session produces the render evidence; counters for
the GPU copy gates are already instrumented (see `docs/ZERO_COPY_GATES.md`).

## Grid / audit finding #11 (native path-fill ≤ 8 vertices)

Coverage-debt measurement (attempted/promoted/fallback counters) is still
open; widening the native promotion limit comes after measurement.
