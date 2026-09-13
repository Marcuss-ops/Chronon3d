# TICKET-HOTPATH-AUDIT-N1-N8 — 2026-09-13 audit remediation wave (daemon / render hot path)

**Stato:** CODE DONE + BUILD PASS (`linux-fast-dev`, `linux-video-test`) — suite minima Verde tranne i 13 fallimenti **pre-esistenti** di `backend_registry` (vedi Verification)
**Owner:** working session (repo owner + agent)
**Created:** 2026-09-13
**Baseline:** working tree on `main` (in-flight `text_batch` demolition already staged)
**Predecessor:** `fixes.md` (prior audit, committed at `5af91243a`)

## Purpose

Closes the P0/P1/P2 findings N1–N8 of the 2026-09-13 read-only audit. Only the
code delta is described here; canonical state lives in `docs/CURRENT_STATUS.md`
and `docs/FOLLOWUP_TICKETS.md` (max one synthetic line each).

The first session that produced this ticket was instructed "do not build"; a
later session built both presets and ran the minimum suite, so the deltas below
are **CODE DONE + BUILD PASS** and the residual red is the pre-existing
`backend_registry` blocker (section *Verification*). Nothing here claims a green
suite.

## Findings and deltas

| Id | Pri | Finding | Delta |
|---|---|---|---|
| N1 | P0 | daemon warm device-session map mutated without a lock from concurrent IPC workers (`serve_concurrent` = one thread per connection; RENDER_JOB holds no IPC mutex) | `m_device_sessions_mutex` guards `find` + `emplace` in `warm_renderer_for_device()` |
| N2 | P0 | one `m_ipc_state_mutex` serialized "concurrent" IPC across a full compile and a disk write, contradicting the stated design | removed; state split per domain: `m_prepared_mutex` (prepared slot, now `shared_ptr`), `m_engine_mutex` (engine mutation), `m_overlay_mutex` (overlay execution), atomic counters, lock-free STATUS |
| N3 | P1 | plan-level `TextBatch` plane (enum + struct + variant arm + visit arms + builder + executor arm) survived with zero producers | deleted; the live path is `RenderBackend::draw_text_batch`, untouched |
| N4 | P1 | Vulkan batched surface ops hold both contention-domain shards per op, under a comment claiming an "unlocked hot path" | **decoupled**: generation token + recording leases in `VulkanSurfaceAuthority`, two-phase `run_batched_surface_op`, plan-wide lease in `begin_plan_batch`, stale claim corrected; the residual coupling is now measured (`dual_lock_*`) — see `TICKET-PRE-SLANG-FOUNDATION.md` §4 |
| N5 | P1 | `RenderPipeline` threw `std::runtime_error` for expected scheduling states | `FrameSchedulingCode` enum + `Result<std::shared_ptr<Framebuffer>, FrameSchedulingCode>` at both single-frame entry points |
| N6 | P2 | `frame_batch.descriptor_sets` grew by `push_back` per pass per frame with no ceiling | `reserve(plan.pass_count())` at plan batch begin (capacity retained by `clear()`) |
| N7 | P2 | `SampledTextParameters::font_index` written as 0 and never read; `SampledShapeParameters` / `SampledEffectParameters` (heap `std::vector`) never constructed inside the hottest per-layer variant | all three removed; the variant now contains only alternatives `TypedProgramSampler` produces |
| N8 | P2 | default backend/codec facts duplicated across daemon, resolver fallback chain and CLI defaults | `kDefaultBackend` / `kAutoCodec` / `kDefaultCodec` / `kDefaultGpuBackend` owned by `video_execution_resolver.hpp`; all four sites read them |

## N1 — device-session map lock

`DaemonService::warm_renderer_for_device()` did `m_device_sessions.find()` /
`emplace()` on a plain `std::unordered_map` reached from the RENDER_JOB path,
which deliberately does not take any IPC mutex, while `serve_concurrent()`
runs one worker thread per client connection. Two concurrent RENDER_JOB
submissions therefore raced on the same map (UB, not merely a duplicate
warm-up). The map now has its own lock; the renderer construction stays inside
the critical section so a device can never be warmed twice.

## N2 — per-domain IPC state

`m_ipc_state_mutex` guarded four unrelated facts at once. Now:

| Domain | Owner | Held across |
|---|---|---|
| Prepared-plan slot | `m_prepared_mutex` | handle read / swap only (`shared_ptr` snapshot) |
| RenderEngine mutation | `m_engine_mutex` | asset import, `prepare()` |
| Overlay execution | `m_overlay_mutex` | `render()` + `save_image()` |
| Counters | atomics | nothing |

STATUS holds no execution lock. RENDER_OVERLAY snapshots the prepared handle
under `m_prepared_mutex` and then renders lock-free, so a concurrent
PREPARE_PLAN can no longer invalidate the plan mid-render (the previous
`unique_ptr` member made that impossible without holding the global lock for
the whole render). `m_overlay_mutex` exists because a `PreparedRenderJob` owns
exactly one single-job `JobSession`: two overlapping overlays on the *same* plan
are genuinely unordered and must serialize, but nothing else waits on it.

Deliberate residual: `m_engine_mutex` still serializes a compile against an
asset prefetch. `RenderEngine` has no documented concurrent-mutation contract,
so inventing one here would be an unbacked relaxation, not a fix.

## N3 — TextBatch demolition (complete)

Deleted: `GpuPassKind::TextBatch` (value 10 is reserved by comment so a future
pass cannot silently inherit it), `struct TextBatchPass`, its `GpuPassParams`
arm, `detail::collect_surface_refs` / `detail::collect_accesses` overloads,
`GpuCommandPlanner::text_batch()`, and the `executor.cpp` dispatch arm.

The variant no longer embeds two `std::vector`s that no producer could ever
fill. `RenderBackend::draw_text_batch` (reached by `gpu_text_run.cpp:1100` and
`node_runner_fused_batch.cpp:631`) is the live text-batch path and is untouched.

## N4 — decoupled surface store and submission ring

The coupling was removed by making the resolved image survivable without the
store lock, rather than by weakening any check:

1. **Generation token.** `VulkanSurfaceAuthority` keeps a fixed array of atomic
   slot tokens (stable addresses, so a lease can read its slot's token with no
   lock) plus a store-guarded pin count. Every erase and every
   re-materialization bumps the token; `prune_unused_slots()` skips pinned slots
   and `materialize_binding()` refuses to re-materialize one.
2. **`SurfaceLease`.** A recording op resolves its surfaces under the store lock
   and leaves with `{image, slot, token}`. `resolve_image()` then serves the
   leased pointer for the rest of the recording window, so the op drives the
   submission ring with the store lock released.
3. **Two-phase `run_batched_surface_op`.** The legacy per-op path (the call owns
   the batch) is unchanged and still holds both domains. The decoupled path is
   taken by an already-active batch: it leases `touched` + `read_surfaces`, takes
   only `submission_ring.mutex`, revalidates the token across that hand-off
   (a stale token fails loudly instead of recording onto a recycled image), then
   records. `download_surface()` still resolves under the store lock.
4. **Thread-local recording context.** `recording_resolve_context()` is what
   decides *how* `resolve_image()` resolves. It is thread-local, not a backend
   flag: a plan batch keeps its leases pinned between passes, so a backend-wide
   flag would make a concurrent `download_surface()` — which legitimately holds
   the store lock — refuse to read the store.
5. **Lease lifecycle is exactly symmetric.** `begin_plan_batch()` pins one count
   per element of the compiled request list and `end_frame_batch_unlocked()`
   releases *the same list*, because the lease map is keyed by handle while the
   pin counts per element: a duplicated surface in `resources.requests` would
   otherwise keep its lease alive forever. A net `release_all_recording_leases_locked()`
   then guarantees no residual pin at batch end, where no recording can be in
   flight because both domains are held.

## N5 — Result at the pipeline boundary

```cpp
enum class FrameSchedulingCode : std::uint8_t {
    NoFreeFrameSlot, PublishEvaluatedFailed, AcquireRenderSlotFailed,
    PublishRenderedFailed, ReleaseEncodedSlotFailed, ExecutorFailed,
};
using FrameRenderResult =
    ::chronon3d::Result<std::shared_ptr<Framebuffer>, FrameSchedulingCode>;
```

`RenderPipeline::render()` / `render_compatibility()` return `FrameRenderResult`
instead of throwing for those states. Cleanup semantics are preserved exactly:
the states that used to throw *outside* the render `try` block (`NoFreeFrameSlot`,
`PublishEvaluatedFailed`, `AcquireRenderSlotFailed`) return without the
`execution.fail()` the catch handler performed, and the states that used to
throw *inside* it (`ExecutorFailed`, `PublishRenderedFailed`,
`ReleaseEncodedSlotFailed`) keep the same `execution.fail()` + abort sequence via
a local `fail_with` lambda. Only genuinely unexpected backend exceptions still
propagate.

`PreparedRenderJob::render()` preserves its public throw contract for OPP
callers by translating the code into `render_pipeline`'s canonical session error
plus the code name — callers can now distinguish recoverable from fatal without
string-matching an exception message.

## N7 — frame-parameter vocabulary

`SampledTextParameters` is now `{text_index}`. `SampledShapeParameters` and
`SampledEffectParameters` are gone. Both were unconstructable by
`TypedProgramSampler` (only Color/Image/Video/Text are produced), so every
`GpuPass`/`SampledLayerParameters` variant paid their footprint for nothing —
and `SampledEffectParameters` carried a heap `std::vector<float>` inside the
per-layer-per-frame variant, a latent allocation trap on the hottest path.

## Verification

**Built** (ccache-warm, `-j12`, 24 core / 29 GB — no OOM):

| Preset | Targets | Result |
|---|---|---|
| `linux-fast-dev` (Vulkan OFF) | `chronon3d_backend_registry_tests`, `chronon3d_render_graph_tests` + the libraries behind N1/N2/N5/N7/N8 | `NINJA_EXIT=0`, 0 `error:` |
| `linux-video-test` (Vulkan ON) | Vulkan backend library, `backend_registry`, `compositor`, `render_job_contract`, `render_graph`, `vulkan_surface_materializer` | `NINJA_EXIT=0`, 0 `error:` |

**Minimum pre-push suite** on `linux-video-test`: `compositor` PASS,
`render_job_contract` PASS, `render_graph` PASS (79 s).
Module suites that cover the touched code on `linux-fast-dev`:
`gpu_command_plan` (N3) PASS, `frame_execution_slot` (N5) PASS, `cli`
(daemon TU compile+link, N1/N2) PASS.
`vulkan_surface_materializer`: 13 casi, 11 PASS, **2 FAIL pre-esistenti**
(asserzioni identiche a `HEAD`; il diff di questo ticket è solo additivo).
I 6 casi lease/token (4 nuovi) sono tutti PASS.

**`backend_registry`: 59 casi, 46 PASS, 13 FAIL — pre-esistenti, non causati da
questa onda.**

- Il tick `TICKET-AUDIT-2026-09-12-REMEDIATION.md §7` documenta la stessa suite
  a 58/46/**12** (14 assertion) con dominante
  `std::logic_error("Vulkan planned surface physical slot is already reserved")`
  da `bind_planned_exact`, introdotto da `077a44275` (2026-09-05): nessuno dei
  due P0 né N4–N8 lo sfiora.
- Il 13° caso è `cached text-run builds glyphs on both styled cache miss and hit
  via Vulkan`: in `gpu_text_run_cached.inc:183` la styled cache dipende da
  `ctx.runtime`, che il test non imposta — quindi frame 1 è di nuovo MISS e le
  due `CHECK` della HIT (2 assertion) falliscono. Il caso esiste identico a
  `HEAD` (`test_backend_registry_vulkan_text_run.cpp:103`) e il path non tocca
  alcuna modifica di questa onda.
- Le due correzioni di correttezza fatte in questa sessione (pin/release
  accoppiati; contesto di recording thread-local) **non cambiano il conteggio**:
  59/46/13, 716 assertion (700 PASS), identico prima e dopo.
- **Architecture gate: RUN e VERDE — `GATE_PASS 185/185`** (poi 188/188 dopo
  `TICKET-PRE-SLANG-FOUNDATION`). Correzione di un errore della sessione
  precedente: il gate **è** eseguibile su questa macchina, serve un Python con
  `tomllib` (≥3.11). Il `python3` di default è 3.10, ma `python3.11` è presente:
  `python3.11 tools/check_architecture.py`. `tools/architecture_gate_selftest.sh`
  seleziona già da sé un interprete valido; la lane CI resta l'autorità finale.

Static checks: zero remaining references to `GpuPassKind::TextBatch` /
`TextBatchPass` / `text_batch()` in `src/`, zero remaining references to
`SampledShapeParameters` / `SampledEffectParameters` /
`SampledTextParameters::font_index`, zero remaining `m_ipc_state_mutex`
references.

## Demolition Debt

### DD-1 — plan-level `TextBatch` plane (N3)

| Campo | Contenuto |
|---|---|
| **Owner** | runtime command-plan owners (working session) |
| **Reason** | none — this was a removal, recorded so the reserved enum value 10 has an owner |
| **Exit condition** | satisfied: zero producers/consumers repo-wide (grep-verified); the live text path is `draw_text_batch` |
| **Equivalence test/gate** | `backend_registry` + `render_graph` suites green on the same SHA; `sizeof(GpuPass)` shrinks by the removed `std::vector` pair |
| **Removal scope** | enum value 10 stays reserved; `render_surface.hpp`'s `GlyphStatic` / `TextRunDynamic` stay (owned by the live backend path) |
| **Status** | `REMOVED` |

### DD-2 — Vulkan surface/submission domain coupling (N4)

| Campo | Contenuto |
|---|---|
| **Owner** | Vulkan backend owners (working session) |
| **Reason** | `run_batched_surface_op` / `begin_plan_batch` resolved surface state *and* recorded into the submission ring in the same call, so they held `surfaces.mutex` + `submission_ring.mutex` together. The blocker was that no token survived the surfaces→submission hand-off |
| **Exit condition** | satisfied on the recording path: `SurfaceLease` + the per-slot atomic token are revalidated between the state lookup and the first recorded command, so a create/release/erase of a leased slot is impossible (`require_not_leased`) and a token change is rejected at the hand-off. The dual lock now remains only where the call *owns* the batch (legacy per-op submit) and in `download_surface()` (whose correct lock is the store) |
| **Equivalence test/gate** | 4 new CPU-only cases in `test_vulkan_surface_materializer` (`a leased slot survives prune_unused_slots`, `a leased slot cannot be re-materialized`, `the generation token invalidates a stale lease`, `nested leases on one slot are reference counted`) + `compositor` / `render_graph` green on `CHRONON3D_ENABLE_VULKAN=ON`; `legacy_frame_batch_op_count()` unchanged |
| **Removal scope** | the dual `std::scoped_lock(surfaces.mutex, submission_ring.mutex)` was removed from all 12 batched recording wrappers (`composite`/`copy`/`fill_*`/`transform(_affine)`/`blur`/`glow`/`color_adjust`/`matte`/`draw_text_run*`/`draw_text_batch`/`execute_layer_batch`/`composite_legacy_surface`); `vulkan_backend_stats.cpp` and `wait_for_pending_submissions()` are submission-only; the plan batch leases instead of locking. **Still dual by need, not by oversight** (each documented at the site): the legacy per-op path (owns the whole batch transition), and the lifecycle/transfer ingress — `create_cuda_external_surface`, `copy_surface_to_cuda_encoder`, `prepare_cuda_surface_for_vulkan`, `release_surface`, `release/retire_frame_transient_surfaces`, `upload_surface` (×4) and `download_surface`, which resolve *and* submit in one call. Converting those to leases is the same pattern but a separate, unvalidated change |
| **Status** | `REMOVED` (recording path) / `ACTIVE` only for the legacy per-op ingress, which DD-1's batch migration owns |

### DD-3 — `m_engine_mutex` serialization of compile vs asset prefetch (N2)

| Campo | Contenuto |
|---|---|
| **Owner** | daemon owners (working session) |
| **Reason** | `RenderEngine` has no documented concurrent-mutation contract; asset import and plan preparation currently share one domain |
| **Exit condition** | `RenderEngine` publishes an explicit concurrency contract for `assets()` / `prepare()` (or an ADR declares them independent) and the daemon drops the shared mutex |
| **Equivalence test/gate** | daemon IPC suite green with a concurrent PREFETCH_ASSET + PREPARE_PLAN case on the same SHA |
| **Removal scope** | `m_engine_mutex` and its two acquisition sites |
| **Status** | `ACTIVE` |
