# TICKET-DEFERRED-SURFACE-SLOT-HINT — exact ownership slot for deferred surface releases

**Stato:** OPEN (deliberate conservative implementation; audit pending)
**Owner:** working session (repo owner + agent)
**Created:** 2026-09-28
**Baseline:** `main@52bce4ac2` (local `main`; not pushed)
**Related:** `52bce4ac2 fix(vulkan): stop draining the submission ring per frame`,
TICKET-VK-LOCK-SHARDING (deferred-release drain split)

## Purpose

`52bce4ac2` removed the per-handle blocking `wait_for_pending()` from
`flush_deferred_surface_releases()` and replaced it with non-blocking
`vkGetFenceStatus` polling plus a bounded re-queue, which took
`frame_batch_drain_wait_us` from ~876 s/job to ~0 on the 25-job phrase corpus.

That change left one knowingly inexact decision in place: **which submission
slot a deferred release is gated on**. This ticket records the reasoning, the
bounds, and the audit required before it can be tightened.

## Current behaviour

`VulkanBackend::Impl::defer_surface_release(handle)`
(`src/backends/vulkan/vulkan_backend_impl.hpp`) stamps **the slot hinted by
`frame_batch.next_slot`**, i.e. the slot the current or upcoming batch records
into — not the slot that actually referenced the surface:

```cpp
const std::size_t slot = frame_batch.next_slot;
surfaces.deferred_surface_releases.push_back({handle, slot});
```

`flush_deferred_surface_releases()` (in
`src/backends/vulkan/vulkan_surface_store_private.cpp`) polls the frame fences
non-blockingly, retires every completed slot, then walks the pending list:

* if `item.slot < FrameBatchState::kSlotCount && frame_batch.in_flight[item.slot]`
  the item is **re-queued** for the next flush;
* otherwise the surface binding is erased and the handle counted in
  `stats.surface_releases`;
* if the pending list grows past `kOverflowCap = 2 * kSlotCount`, a blocking
  `wait_for_pending()` is taken as a bounded fallback.

## Why the hint is deliberately conservative (and safe)

* **Stale-but-too-new hints can only delay, never free early.** The flush
  re-checks the hinted slot before erasing the binding, so a handle whose real
  owner is an older slot waits at most one extra slot rotation.
* **Reusing the binding slot instead would be wrong.** Physical slots are
  aliased across frames; a handle's binding slot may already have been recycled
  by the time the deferred release is drained, so trusting it could free live
  storage while a newer submission still reads it.
* **The failure mode is bounded.** Worst case the pending list grows until
  `2 * kSlotCount`, at which point the code falls back to the (correct but
  slow) blocking drain, so this cannot leak without bound.

## What to audit before tightening

1. **Exact recording slot.** Establish whether the recording site (the frame
   batch that referenced the handle) can stamp the handle with the slot that
   actually owns the reference, instead of `next_slot`. Both the hint and the
   true owner are cheap to carry: `DeferredSurfaceRelease{handle, slot}` already
   has the field.
2. **Aliasing.** Confirm the invariant "a physical slot is never reused while an
   older slot's submission is in flight" — the hint is only safe to tighten if
   that holds for every path that recycles slots (retire, prune, reclaim).
3. **Retained bindings high-water mark.** Instrument the maximum length of
   `surfaces.deferred_surface_releases` and of `surfaces.surface_bindings`
   across a long job. If the deferral window keeps bindings alive for an extra
   rotation, transient VRAM retention grows with it.
4. **Overflow path.** Show that hitting `kOverflowCap` is unreachable on the
   production corpus once the hint is exact; if it stays reachable, decide
   whether the cap should be raised or the overflow should be counted.

## Acceptance

* The slot carried by a deferred release is the slot that actually referenced
  the handle, or the conservative `next_slot` hint is documented as permanent
  with a measured bound on the deferral window.
* `vulkan_telemetry` / the timing sidecar expose the retained-binding and
  pending-queue high-water marks used in (3).
* No regression in `frame_batch_drain_wait_us` (must stay at the post-`52bce4ac2`
  level of ~0 on the phrase corpus) and no surface freed while `in_flight`.

## Non-goals

* Reworking the frame-batch slot ring itself (tracked by TICKET-VK-LOCK-SHARDING).
* The `queue_depth_average == 1` / NVENC under-utilisation finding, which is a
  separate pipeline-depth body of work.
