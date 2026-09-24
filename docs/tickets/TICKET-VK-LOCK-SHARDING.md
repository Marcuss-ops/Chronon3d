# TICKET-VK-LOCK-SHARDING — Vulkan backend lock sharding + audit remediation (P1–P5)

**Stato:** IN PROGRESS (code landed; build/test verification pending)
**Owner:** working session (repo owner + agent)
**Created:** 2026-09-06
**Baseline:** `main@bf8efb0c0` (local working tree; not yet pushed)

## Purpose

Remediates the technical audit findings P1–P5 (plus the executor hot-path
scratch finding and repo hygiene) produced against the Vulkan backend,
runtime prepared-asset cache, media mux checksum path and node executor.
Only code/delta entries live here; canonical state is reported in
`docs/CURRENT_STATUS.md` / `docs/FOLLOWUP_TICKETS.md`.

## P1 — Global `recursive_mutex` → three domain shards (code DONE)

`VulkanBackend::Impl` no longer owns one `api_mutex` (25 acquisition sites
across `vulkan_backend.cpp` / `vulkan_backend_surface_api.cpp`).  It now
owns three non-recursive domain shards mirroring the existing authorities:

| Shard | Guards |
|---|---|
| `surface_mutex` | `VulkanSurfaceAuthority` state (bindings, physical images, deferred releases, transient scratch) |
| `upload_mutex` | `VulkanUploadAuthority` state (upload-ring slots, staging buffer) |
| `submission_mutex` | submission ring + frame-batch ring + replay slots + queue ops + timeline values |

Acquisition order is fixed: `surface → upload → submission`, never reversed
(comment + `AllShardsGuard` RAII in `vulkan_backend_impl.hpp`).

Per-site classification (semantics preserved per op):

- **Surface-only**: `create_surface`, `is_native_surface_valid`,
  `export_cuda_external_memory` (const store read) take `surface_mutex`
  alone and can therefore proceed while a download waits on the GPU.
- **Submission-only**: `begin_command_batch`, replay recording/submit take
  `submission_mutex` alone (pure submission-ring state).
- **Cross-domain** (everything that records, drains or uploads):
  `AllShardsGuard` (all three, canonical order) — batch transitions, surface
  ops via `run_batched_surface_op`, release/transient drains, uploads,
  downloads, CUDA interop paths.

Fence-wait extraction: shared `VkFence` wait/reset cycles stay inside the
shards because a fence supports exactly one wait/reset owner at a time —
shedding them would let a concurrent batch driver double-wait the same
fence.  Multi-waiter-safe **timeline-semaphore waits are shed**: a blocking
`download_surface()` (waits on `vkWaitSemaphores(timeline, signal)` with the
submission shard released; surface+upload stay held around the shared
staging readback) and `wait_upload()` (`wait_upload_ticket()` runs the
timeline wait through `wait_outside_locks()`) no longer serialize every
render thread behind a GPU stall.  `release_surface_now()` is split from its
drain; `flush_deferred_surface_releases()` drains **once** for the whole
deferred batch instead of once per handle (kills the blocked-drain chain
flagged in the audit minor findings).

Recursion removal: `run_batched_surface_op` previously re-entered public
`begin/end_frame_batch()` under the recursive lock.  The frame-batch
transitions now live in internal `begin_frame_batch_locked()` /
`end_frame_batch_locked()` (all-shards held) used by both the public entry
points and the per-op path.

## P2 — Per-op frame-batch enforcement (code DONE)

`run_batched_surface_op` keeps its legacy per-op submit semantics (no
behavior change for existing callers) but now **observes** the path: every
op that owns its own frame batch increments
`legacy_frame_batch_op_count` and logs a warn (first hit + every 1000th).
Public accessors `legacy_frame_batch_op_count()` /
`unified_shard_lock_count()` expose both counters.

## P3 — Mux checksum catch triage (code DONE)

`src/media/video/mux/packet_assembler.cpp` `close_and_checksum()`:
`std::bad_alloc` is rethrown (OOM must not masquerade as file corruption);
`std::exception` and unknown exceptions are logged via spdlog before the
`false` return, so a failing checker is distinguishable from corrupted data.

## P4 — Prepared-asset identity SSOT + observability (code DONE)

`src/runtime/render_runtime.cpp` had two drifted key builders
(`mesh_manifest_key` vs `prepared_mesh_key` — one unique-erased, the other
did not) and a silent cache-miss fallback.  Now:

- One canonical `mesh_identity_key(manifest)` (sorted, unique) is used by
  BOTH `publish_prepared_assets()` (keyed off the **source manifest**, new
  parameter — the snapshot may legitimately skip optional/duplicate entries)
  and `prepared_assets_for()`.  Publish/query can no longer drift.
- Hit/miss counters (`prepared_assets_cache_hits/misses()`) plus a
  once-per-thread warn when a content mismatch forces silent re-preparation.

## P5 — Dead per-node timing params → non-constructible sink (code DONE)

The seven `(void)`-discarded `double* out_*_ms` parameters of
`execute_single_node` are collapsed into one `const NodeTimingSink*`
(`node_runner.hpp`) where `NodeTimingSink` is non-constructible — the only
legal argument is `nullptr`, so no caller can wire up expectations the
execution contract no longer fulfils.  Both dispatch sites pass
`/*out_timing_sink=*/nullptr`.  Removing even the placeholder is deferred
to the next SDK ABI-breaking window (per audit).

## Hot path — worker-local reusable-inputs scratch (code DONE)

`execute_single_node` gathered reusable inputs into the per-node cloned
context's `reusable_inputs` vector (fresh allocation per node per frame
when inputs were reused).  It now gathers into a function-local
`thread_local` scratch and **move-assigns** it into `node_ctx` (pointer
steal, zero per-node heap churn).  Safe under nested parallelism: the fill
loop performs no re-entrant calls, and nested executions own their
move-stolen buffer afterwards.

## Hygiene — repo-root artifacts (code DONE)

`.gitignore` pins the audit's root-level run artifacts by name
(`/--hot-path.partial.mp4`, `/--hot-path.log`, `/test_output_validity.png`,
`/build_cleanup.log`) so they are recognized at a glance (they were already
matched by the broad `*.log`/`*.png`/`*.mp4` rules).

## Demolition Debt

| Campo | Contenuto |
|---|---|
| **Owner** | Vulkan backend owners (working session) |
| **Reason** | `run_batched_surface_op`'s legacy per-op frame-batch path (auto begin → submit per call) still exists for surface-API callers that have not migrated to the explicit `begin_command_batch()`/`end_command_batch()` contract; the lock sharding could not remove it without changing per-op submit semantics (P2) |
| **Exit condition** | `legacy_frame_batch_op_count()` stays zero across the backend_registry / render_job_contract / render_graph suites AND no production caller outside the compiled command-plan executor reaches `run_batched_surface_op` with an inactive frame batch |
| **Equivalence test/gate** | backend_registry + compositor + render_job_contract + render_graph suites green on the same SHA; Vulkan object library compiles in a `CHRONON3D_ENABLE_VULKAN=ON` build |
| **Removal scope** | auto-begin/end inside `run_batched_surface_op` (legacy per-op submit), the `owns_batch` branch, and — when the SDK ABI window opens — the `NodeTimingSink` placeholder param (P5) |
| **Status** | `ACTIVE` |

## Audit 2026-09-13 — N4 status (decoupling landed)

The 2026-09-13 audit re-flagged the cross-domain acquisition above as N4. The
sharding itself works as designed; what was wrong was one comment at
`vulkan_backend_surface_api.cpp:36-38` claiming a "single-owner unlocked hot
path" while the wrapper underneath holds `surfaces.mutex` +
`submission_ring.mutex` for the whole operation.

Both halves are now done. The claim is replaced by the actual invariant
(non-re-entrancy of the `*_unlocked` cores, fixed acquisition order), and the
recording path no longer acquires the two domains together:

- `VulkanSurfaceAuthority` grew a per-slot generation token plus pin counts
  (`SurfaceLease`); pruning and re-materialization refuse a leased slot.
- A recording op leases its surfaces under the store lock, releases it, takes
  only `submission_ring.mutex`, and revalidates the token across that hand-off
  before the first recorded command.
- `begin_plan_batch()` leases the whole compiled surface set for the batch;
  `end_frame_batch_unlocked()` releases the same list and then nets out any
  residual pin.
- `wait_for_pending_submissions()` and the stats reads are submission-only.

The remaining dual acquisitions are the ones that genuinely need both domains:
the legacy per-op path, which owns the batch and drives
begin→record→submit→drain, and `download_surface()`, whose correct lock for
`resolve_image()` is the store. Detail and evidence: audit DD-2 (the N1-N8
remediation ticket is closed — its record lives in git history).

## Verification

Built on this host: `linux-video-test` (Vulkan ON) and `linux-fast-dev`
(Vulkan OFF), `-j12`, 0 errors. `compositor`, `render_job_contract` and
`render_graph` PASS; `vulkan_surface_materializer` 11/13 (2 pre-existing
failures, unchanged from `HEAD`);`backend_registry` 46/59 with the 13 failures documented as pre-existing in `TICKET-AUDIT-2026-09-12-REMEDIATION.md §7`.
`tools/check_architecture.py` NOT RUN (no `tomllib` in this environment).

### Re-verified 2026-09-24 (working session)

The stale `backend_registry` failure count above is superseded. On the
`linux-video-test` lane (Vulkan ON) with the current tree:

- `chronon3d_backend_registry_tests` — **62/62 cases, 920/920 assertions,
  PASS** (rebuilt `-j12`, 417 edges, 0 errors). The earlier 46/59 attributed
  the 13 failures to a hand-built `RenderGraphContext` in the canary harness
  that never wired `ctx.gpu_styled_glyph_cache` (production wires it in
  `scene_context_setup.cpp`); with the runtime wiring in place the suite is
  fully green. The two placement defects of §7 no longer reproduce here.
- Host note: this run logged `[Vulkan] CUDA interop extensions unavailable;
  native video surface interop disabled` — the Vulkan device path executes, but
  Vulkan↔CUDA native-surface interop is not exercised on this host.

The **Demolition Debt** row (legacy per-op `run_batched_surface_op`) stays
`ACTIVE`: its exit condition (`legacy_frame_batch_op_count()` zero across
backend_registry / render_job_contract / render_graph) is not closed by this
run, and it is not a lock-sharding defect — see the row above.
