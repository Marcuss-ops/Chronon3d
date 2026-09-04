# Vulkan backend decomposition census

This document is the Wave 2 responsibility census for `VulkanBackend::Impl`.
It records the as-built ownership on `main` before the remaining Vulkan backend
components are extracted.  It is intentionally descriptive: component moves
must preserve behavior and must not create a second allocation, synchronization,
or kernel authority.

## Gate

Wave 2 starts only after the synchronization single-authority migration.  The
compiled Vulkan path now consumes the canonical `ResourceTransition` stream and
materializes it through Synchronization2; legacy compiled barrier planning is no
longer a decomposition concern.

## Target

```text
VulkanBackend::Impl
 ├─ DeviceContext
 ├─ DescriptorAllocator
 ├─ SubmissionRing
 ├─ SurfaceMaterializer
 ├─ UploadRing
 ├─ KernelStore
 └─ CudaInterop
```

`VulkanBackend::Impl` remains the orchestration boundary.  A component may own
mechanism and Vulkan objects, but it must not duplicate an upstream policy
authority.

## Current `Impl` field census

### API serialization and diagnostics

| Field | Current owner | Lifetime | Mutable/shared | Target |
| --- | --- | --- | --- | --- |
| `api_mutex` | `Impl` | backend | yes; serializes public backend entry points | `Impl` orchestration |
| `debug_context` | borrowed pointer in `Impl`; unique owner is `VulkanBackend` | backend | setup/debug only | borrowed service; not a second Vulkan-state owner |
| `stats` | `Impl` | backend | yes, under backend serialization | `Impl` telemetry |

### Device and queue state

| Field | Current owner | Lifetime | Mutable/shared | Target |
| --- | --- | --- | --- | --- |
| `instance` | copied handle in `Impl`; real destruction is in `VulkanBackend` | backend | effectively immutable | `DeviceContext` |
| `physical_device` | copied handle in `Impl`; selection state also exists in `VulkanBackend` | backend | immutable after construction | `DeviceContext` |
| `device` | copied handle in `Impl`; real destruction is in `VulkanBackend` | backend | immutable after construction | `DeviceContext` |
| `queue` | copied handle in `Impl`; matching state exists in `VulkanBackend` | backend | externally synchronized by backend | `DeviceContext` |
| `queue_family` | copied scalar in `Impl`; matching state exists in `VulkanBackend` | backend | immutable | `DeviceContext` |
| `command_pool` | copied handle in `Impl`; destroyed by `VulkanBackend` | backend | command-buffer allocation/reset | `DeviceContext` |
| `calibrated_ts_supported` | `Impl` copy of wrapper discovery state | backend | immutable | `DeviceContext` |
| `pfn_get_calibrated_timestamps` | `Impl` | backend | initialized once | `DeviceContext` |
| `gpu_timestamps_calibrated` | `Impl` | backend | initialized once | `DeviceContext` |
| `calibration_gpu_ts` | `Impl` | backend | initialized once | `DeviceContext` |
| `calibration_cpu_trace_ns` | `Impl` | backend | initialized once | `DeviceContext` |
| `calibration_max_deviation` | `Impl` | backend | initialized once | `DeviceContext` |
| `timestamp_period_ns` | `Impl` | backend | initialized once | `DeviceContext` calibration metadata |
| `timestamp_valid_bits` | `Impl` | backend | initialized once | `DeviceContext` calibration metadata |

**Ownership problem:** instance/device/queue/queue-family/command-pool state exists at
both the public `VulkanBackend` wrapper and `Impl`.  The wrapper performs the
actual instance/device/command-pool destruction while `Impl` consumes copied
handles.  W2.2 must collapse that into one RAII owner rather than add another
view layer.

### Descriptor state

Current canonical storage is `VulkanDescriptorAuthority descriptor_arena`.
`Impl` keeps compatibility references only:

- `descriptor_layout`
- `text_tile_bin_descriptor_layout`
- `text_tile_raster_descriptor_layout`
- `descriptor_pool`
- `descriptor_set`
- `glow_descriptor_sets`

`VulkanDescriptorAuthority` also owns the three resettable per-frame allocator
families (`pass`, `text_tile_bin`, `text_tile_raster`).

Target: rename/narrow the component to the Wave 2 `DescriptorAllocator`
responsibility after compatibility references have zero production callers.
There must remain exactly one descriptor allocation authority.

### Submission state

Current canonical storage is `VulkanSubmissionAuthority submission_ring`.
It owns:

- frame `command_buffers[3]`
- frame `fences[3]`
- frame `in_flight[3]`
- `submitted_pass_counts[3]`
- borrowed references to the three descriptor-allocator families
- recorded per-batch descriptor sets
- `pass_count`
- current `CommandPlan*`
- three replay slots, each with command buffer, fence, in-flight state and
  parameter buffer
- replay prepared/next-slot state
- immediate command buffer/fence
- timeline semaphore and timeline counters
- timestamp query pool
- command-batch active/started state

`Impl` exposes compatibility references/aliases for `frame_batch`, replay slots,
command buffer, fence, timeline semaphore, timestamp pool and timeline counters.

**Slot census:** frame and replay rings both rotate over depth 3 but are not
semantically identical.  Frame slots own live-recording state and per-frame
descriptor allocator relationships; replay slots own pre-recorded command
buffers plus persistently reusable parameter buffers.  Upload slots are
explicitly outside this component because transfer staging lifetime is
independent of graphics/compute submission rotation.

Target: `SubmissionRing` owns submission-lifetime primitives.  Timestamp query
objects belong here; timestamp calibration metadata belongs to `DeviceContext`.
Do not merge replay/upload storage merely because the ring depths currently
match.

### Upload state

Current canonical storage is `VulkanUploadAuthority uploads`:

- one reusable `staging` allocation
- three `UploadSlot`s, each with buffer allocation, command buffer, fence,
  ticket and in-flight bit
- `next_slot`

`Impl::staging` is a compatibility reference to `uploads.staging`.
Upload lifecycle behavior (`wait_upload_slot`, `acquire_upload_slot`,
`ensure_upload_slot`, `submit_upload`, `wait_upload_ticket`) is still implemented
on `Impl`.

Target: `UploadRing` owns upload storage and its reuse/lifetime mechanism.  Its
certification must prove wraparound, alignment, coherent/non-coherent
flush/invalidate correctness and zero steady-state per-frame allocation.

### Kernel state

Current canonical storage is `VulkanKernelStore kernels`:

- `GpuKernelRegistry registry`
- general pipeline layout
- text tile-bin pipeline layout
- text tile-raster pipeline layout

`VulkanKernelStore::destroy()` enumerates the registry itself; it does not keep a
second hard-coded `GpuKernelId` destruction list.  Therefore the
`GpuKernelRegistry` remains the registered-kernel authority.

Target: keep `KernelStore` as Vulkan pipeline/layout storage and destruction
mechanism.  Pipeline resolution must continue through `GpuKernelRegistry`; do
not add a parallel enum-to-pipeline switch or duplicate kernel enumeration.

### Surface state

`Impl` owns `VulkanSurfaceStore surfaces`, a compatibility subclass of
`VulkanSurfaceAuthority<Image, Impl>`.  The current authority stores:

- `physical_surfaces`
- logical `surface_bindings`
- deferred releases
- unplanned-surface set
- dynamic `next_slot`
- `slot_last_access`
- CUDA-ready/export-ready physical-slot sets when CUDA interop is enabled
- an `owner_` pointer back to `Impl`

`Impl` also stores plan-preallocation metadata:

- `plan_preallocated`
- `plan_canvas_width`
- `plan_canvas_height`

and two standalone compatibility images, `dst` and `src`.

**Current violation of the Wave 2 target:** `VulkanSurfaceAuthority` mixes
placement/binding policy with physical Vulkan mechanism.  Its `ensure()` chooses
or reselects slots, while `materialize_binding()` reaches back into `Impl` to
call `make_image()`/`destroy_image()` and mutate stats.  This is the exact seam
W2.6 must split.

Target contract:

```text
CompiledResourceTable / resource table = placement policy
SurfaceMaterializer                  = Vulkan mechanism
```

`SurfaceMaterializer` must receive an already-decided placement in the compiled
path.  It may create/destroy the Vulkan image, image view, memory/export state
and validate compatibility; it must not choose a physical slot.

The explicitly unplanned compatibility path remains demolition debt and must be
kept visibly separate from compiled placement authority.

### Reusable GPU buffer state

`Impl` still owns fixed-size reusable arrays for text/layer execution:

- `glyph_instance_buffers`, hashes and sizes
- `layer_instance_buffers`, hashes and sizes
- `text_run_dynamic_buffers`, hashes and sizes
- `text_tile_count_buffers`
- `text_tile_index_buffers`

The ring size is `FrameBatchState::kSlotCount * 64`.

These are backend-lifetime reusable device buffers, not submission ownership and
not surface placement policy.  They stay in `Impl` during the first Wave 2
extractions unless a later census proves they are upload-ring storage.  Moving
them speculatively would broaden W2.4 and risk changing reuse semantics.

### Memory manager

`VulkanMemoryManager memory_manager` is owned by `Impl` for backend lifetime and
is initialized from the selected Vulkan handles.  It is a service used by
surface, upload, replay-parameter and text/layer buffer mechanisms.  During Wave
2 it should be injected/borrowed by the component that performs a Vulkan memory
operation rather than copied into several components.

### CUDA interop state

CUDA/Vulkan interop is currently distributed rather than component-owned:

- external binary semaphores live on `Image`
- CUDA-ready/export-ready slot sets live in `VulkanSurfaceAuthority`
- external-memory/semaphore creation/export and preparation behavior is exposed
  as `Impl` methods
- public `VulkanBackend` exposes CUDA surface bridge methods

Target: `CudaInterop` owns external-memory/external-semaphore import/export
mechanism, import caches and destruction ordering.  Surface placement remains
outside it.  Registration/import must be once-per-resource, not once per frame.

## Ownership and lifetime summary

| Responsibility | Canonical owner now | Lifetime | Wave 2 status |
| --- | --- | --- | --- |
| Device/queue/command-pool | split between wrapper and `Impl` copies | backend | **not extracted** |
| Descriptor allocation | `VulkanDescriptorAuthority` | backend + per-frame reset | **authority extracted; compatibility aliases remain** |
| Submission | `VulkanSubmissionAuthority` | backend with 3-slot rotations | **partially extracted** |
| Upload storage | `VulkanUploadAuthority` | backend with 3-slot rotation | **storage extracted; behavior still on `Impl`** |
| Kernel storage | `VulkanKernelStore` + `GpuKernelRegistry` | backend | **storage/destruction extracted** |
| Surface placement/materialization | `VulkanSurfaceAuthority` + `Impl` callbacks | backend / resource lifetime | **must split policy from mechanism** |
| CUDA interop | distributed | backend / surface lifetime | **not extracted** |
| Stats/orchestration | `Impl` | backend | stays in `Impl` |

## Mutable shared state

The backend's public operations are serialized by `Impl::api_mutex`.  Within
that serialized domain the major mutable state is:

- submission slot indices/in-flight bits/fences/timeline counters
- descriptor allocator active pools and frame resets
- upload slot index, tickets and in-flight bits
- surface bindings, physical surfaces, deferred releases and compatibility
  allocation cursor
- reusable text/layer buffer contents and hashes
- backend stats

Component extraction must not add component-local mutexes unless an actual
caller census proves concurrent access outside `api_mutex`.  Adding locks during
a no-behavior-change decomposition would hide ownership rather than clarify it.

## Required dependency direction

```text
CompiledResourceTable / CommandPlan
        │
        ├── placement + lifetime policy ───────────────┐
        │                                              ▼
        │                                  SurfaceMaterializer
        │                                      (mechanism)
        │
        └── ResourceTransition[] ──> Sync2 adapter

DeviceContext
  ├─ borrowed by DescriptorAllocator
  ├─ borrowed by SubmissionRing
  ├─ borrowed by SurfaceMaterializer
  ├─ borrowed by UploadRing
  ├─ borrowed by KernelStore
  └─ borrowed by CudaInterop
```

No child component may create a new resource-table, synchronization, descriptor,
or kernel-registry authority.

## Extraction order from the current code

1. **DeviceContext** — collapse wrapper/`Impl` handle duplication and move
   destruction/calibration state behind one RAII owner.
2. **SubmissionRing certification** — retain the already separated frame/replay
   semantics; add explicit wraparound, cancellation and fence-reuse coverage.
3. **UploadRing** — move upload lifecycle helpers next to `VulkanUploadAuthority`
   storage, then prove steady-state reuse/alignment/flush behavior.
4. **KernelStore certification** — add registry-to-pipeline resolution coverage;
   keep registry enumeration as the only pipeline set.
5. **SurfaceMaterializer** — split physical Vulkan image mechanism from
   `VulkanSurfaceAuthority` placement/binding policy; compiled placement remains
   immutable.
6. **CudaInterop** — gather external-memory/semaphore/cache lifetime after the
   surface materialization seam is stable.

This order deliberately avoids moving CUDA/export mechanism before the physical
surface creation/destruction seam has a single owner.

## Exit conditions for W2.1

- every current `Impl` field is assigned to a responsibility or explicitly
  retained as orchestration/service state;
- each responsibility has one stated owner and lifetime;
- mutable shared state and the existing serialization boundary are explicit;
- current partial extractions are distinguished from completed target
  components;
- the target does not create a second policy authority.
