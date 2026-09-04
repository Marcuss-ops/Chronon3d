# Vulkan compiled authority audit

Status: P0 runtime authority hardening implemented on `main`.

## Canonical ownership contract

The compiled execution contract is:

```text
CompiledResourceTable / CommandPlan
    -> exact logical resource + physical slot + lifetime + ResourceTransition
    -> Vulkan materialization
```

The compiler/resource plan decides **what** resource exists, **where** a compiled resource is placed, **when** it is live/released, and **which** transition is required. The Vulkan backend decides only **how** that canonical decision becomes a `VkImage`, barrier, fence/semaphore wait/signal, or queue submission.

## P0.1 caller census: surface placement and lifetime

### Compiled hot path

`VulkanBackend::begin_plan_batch(const CommandPlan&)` is the compiled placement boundary.

For every allocation with a real `physical_slot` it now:

- rejects an invalid `request_index`;
- rejects a slot outside `plan.resources.slots`;
- rejects disagreement between `ResourceRequest.surface` and the allocation surface;
- rejects a surface already marked explicitly unplanned;
- rejects an existing binding to a different physical slot;
- rejects collision with initialized content in the planned slot;
- reserves the compiler-owned slot namespace before compatibility allocation can run;
- calls `bind_surface_to_slot()` with the exact planned slot;
- verifies after materialization that the actual slot is still the planned slot;
- verifies that the compatibility `next_slot` cursor did not change.

`VulkanSurfaceAuthority::bind()` is now strict by construction and always delegates to `bind_planned_exact()`. It never performs pinning, reuse search, diversion, or `next_slot++`.

Invariant:

```text
compiled surface -> planned slot only
```

A violation is a hard error. There is no silent compiled reallocation fallback.

### Explicit non-compiled / compatibility path

`VulkanSurfaceAuthority::ensure()` is the only surface-authority API allowed to choose or re-choose physical placement dynamically. It may reuse a compatible unused transient slot, reuse another unused non-persistent slot, preserve a pinned initialized handle, or allocate through `next_slot++`.

This path is compatibility/Demolition Debt and must not be entered by compiled plan materialization.

CUDA external surfaces are explicitly outside compiled physical placement: their `ResourcePlanner` allocation uses the invalid physical-slot sentinel and Vulkan creates an exportable image through the non-compiled external-surface path. The compiled slot namespace is reserved first so dynamic/external slots cannot silently collide with compiler-owned slots.

### Lifetime / release authority

`ResourceDesc::lifetime` from the plan is preserved during compiled materialization; the backend no longer forces compiled allocations to `FrameTransient`.

`JobPersistent` resources do not participate in transient alias reuse. `FrameTransient` resources may alias only when the planner proves lifetime intervals are disjoint. External resources do not receive compiled physical placement.

The backend may delay destruction until tracked GPU submissions complete, but it does not invent a second compiled lifetime plan. Deferred release is a materialization/safety mechanism, not a placement/liveness authority.

### `slot_last_access`

`slot_last_access` remains in `VulkanSurfaceAuthority` as compatibility state, but the compiled synchronization path does not consume it. Compiled transitions are resolved from `CommandPlan::transitions` instead. Treat `slot_last_access` as Demolition Debt until the unplanned synchronization path is removed; do not add new compiled users.

## P0.2 synchronization authority census

### Compiled graph synchronization

Compiled pass execution calls:

```text
emit_pass_sync
    -> emit_plan_pass_transitions
        -> emit_resource_transition
            -> vkCmdPipelineBarrier2KHR
```

When `frame_batch.command_plan` is present, `emit_pass_sync()` consumes the canonical `ResourceTransition` stream for the current consumer pass and returns. It does not run the unplanned fallback state machine.

This is the authoritative graph synchronization path.

### Backend-only materialization synchronization

The following synchronization remains backend-owned because it implements I/O or internal storage mechanics rather than graph-level dependency decisions:

- upload/readback layout changes (`GENERAL <-> TRANSFER_*`) translated through the same canonical `ResourceTransition` -> Synchronization2 mapper;
- upload-ring fences and timeline-semaphore tickets;
- frame/replay ring-slot fence reuse;
- internal text/layer metadata buffer transfer->compute barriers;
- internal multi-dispatch scratch dependencies;
- Vulkan/CUDA external binary-semaphore import/export handshake.

CUDA waits/signals are materialized at the interop boundary (`cuWaitExternalSemaphoresAsync` / `cuSignalExternalSemaphoresAsync`) and the Vulkan submission collects the corresponding binary semaphore waits/signals once. These do not define a second graph transition plan.

### Compatibility synchronization

`emit_unplanned_compute_sync()` remains for direct/unplanned execution when no `CommandPlan` is attached. It is explicitly non-canonical compatibility behavior and is Demolition Debt.

`emit_command_batch_boundary()` is also a non-compiled command-batch boundary and must not become a parallel compiled transition authority.

### Global synchronization audit

The frame/job path no longer uses `vkDeviceWaitIdle` for transient cleanup. `release_frame_transient_surfaces()` drains only submissions tracked by the backend through `wait_for_pending()`, which waits the standalone fence, in-flight frame-slot fences, and upload-slot fences.

`vkDeviceWaitIdle` remains only in destructive backend teardown, where destroying device-owned Vulkan objects requires a final drain. No `vkQueueWaitIdle` is required by the normal frame/job path.

## Regression coverage

`tests/runtime/test_compiled_resource_authority.cpp` covers:

- canonical compiled resource metadata and release scheduling;
- transient physical-slot reuse only for disjoint lifetimes;
- no physical-slot reuse for overlapping lifetimes;
- `JobPersistent` exclusion from transient aliasing;
- external/CUDA-style resources remaining outside compiled physical placement;
- NV12/P010 plane lowering.

Runtime fail-fast checks in `begin_plan_batch()` additionally cover malformed request indices, out-of-plan slots, physical-slot collision with initialized content, planned-handle movement, unplanned/compiled path mixing, and accidental compatibility allocator entry.

## Demolition Debt and exit conditions

1. **Dynamic surface placement** — remove `VulkanSurfaceAuthority::ensure()` placement policy after every direct caller is either represented in `CommandPlan` or explicitly classified External/non-compiled. Exit condition: compiled execution has zero `ensure()` callers and all dynamic placement callers are enumerated compatibility/external APIs.
2. **Unplanned compute synchronization** — remove `emit_unplanned_compute_sync()` after direct compute execution is migrated to command plans. Exit condition: every frame compute pass has a canonical `ResourceTransition` stream.
3. **`slot_last_access`** — delete after the unplanned synchronization path no longer needs compatibility access state. Exit condition: zero reads/writes outside destruction/reset diagnostics.
4. **Lazy `preallocate_plan_surfaces` naming/docs** — the function currently keeps Vulkan materialization lazy. Reconcile the public name/documentation or remove the hook. This is no longer allowed to influence compiled placement policy.
5. **Teardown idle duplication** — `vkDeviceWaitIdle` still exists in backend/Impl destruction. This is outside the frame P0 contract, but may be consolidated later so teardown has one drain authority.

## Definition of done

P0.1 is satisfied when compiled execution can materialize only the exact physical slot selected by the resource plan and backend lifetime behavior is limited to safe delayed destruction.

P0.2 is satisfied when compiled graph dependencies come only from `CommandPlan::transitions`, Vulkan/CUDA code materializes those dependencies plus backend-specific I/O mechanics, and the normal frame/job path contains no implicit device-global synchronization.
