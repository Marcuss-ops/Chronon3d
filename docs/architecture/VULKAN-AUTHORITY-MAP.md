# Vulkan authority map

This map is the review boundary for `VulkanBackend::Impl`. It records which
state may be changed by each domain and which synchronization is required.
The goal is one semantic authority without turning the whole backend into one
serialized critical section.

| Domain | State owned | Writer | Lock | Readers | Lifetime | Required invariants | First target |
|---|---|---|---|---|---|---|---|
| Device lifetime | `instance`, `physical_device`, `device`, `queue`, command pool, allocator and kernel/device objects | `Impl` construction/destruction and device-loss teardown | No data-plane lock; backend lifetime is the outer invariant | All backend domains while `Impl` is alive | Backend lifetime | No domain outlives the logical device; device-loss poisoning remains fail-closed | Preserve |
| Surface | `VulkanSurfaceAuthority`: logical bindings, physical images, slot selection, CUDA surface flags, deferred releases | `SurfaceAuthority` methods and the surface-store drain | `VulkanSurfaceAuthority::mutex` | Surface validity/count queries, sync translation, submission wait-list assembly while the domain lock is held | Backend lifetime; transient images are retained/retired by this authority | One logical owner per physical slot; planned slots are exact; release never destroys an in-flight image | **Active: extracted lock domain** |
| Submission | `VulkanSubmissionAuthority`: frame/replay ring, command buffers, fences, timeline values, active batch flags | Submission methods and batch state machine | `VulkanSubmissionAuthority::mutex` | Recording, submit, fence/timeline completion, GPU timing | Backend lifetime | One writer per command buffer/ring slot; final queue serialization and device-loss path unchanged | **Active: extracted lock domain** |
| Sync | Canonical resource transitions and Vulkan Synchronization2 emission | `Impl::emit_*` during recording | Submission lock; surface lock when resolving a surface binding | Render operations and validation | Command recording lifetime | Every planned transition resolves through `CompiledResourceTable`; no CPU fallback | Preserve |
| Descriptors | `VulkanDescriptorAuthority`, descriptor layouts/pools and per-frame allocators | Descriptor authority and submission recording | Submission lock for frame allocators; device lifetime for destruction | Kernel recording | Backend lifetime | Descriptor set belongs to the active ring slot and is not reset while in flight | Preserve |
| CUDA interop | External image/semaphore handles and CUDA/Vulkan handoff state | Surface authority for bindings; submission path for wait/signal values | Surface + submission locks for handoff/submit | Encoder bridge and export queries | Backend/job surface lifetime | CUDA-ready/export-ready sets are consumed only by the matching submission; device-loss remains fatal | Preserve |
| Statistics | `VulkanBackendStats`, completed GPU timings and telemetry snapshots | The domain performing the measured work; timing consumer drains completed records | Submission lock for execution/timing counters; surface lock for surface counters; both for combined snapshots | Public stats/telemetry accessors | Backend lifetime | Readers observe a coherent snapshot; no data race between counters and exporters | Follow-up |

## Lock ordering

Operations that touch both domains acquire the locks together with
`std::scoped_lock(surface.mutex, submission.mutex)`. Surface-only queries take
only `surface.mutex`; submission-only replay/timing operations take only
`submission.mutex`. No code may acquire the old backend-wide lock because it no
longer exists.

The submission lock is intentionally still a serialization point: the current
backend owns one command-buffer/ring state machine and one final queue submit
path. The refactor is complete only when independent surface storage work can
run without waiting on that state machine, while command recording remains
serialized exactly where Vulkan requires it.

## Acceptance evidence

- independent surface creation/validity work uses `SurfaceAuthority::mutex`;
- frame recording and final queue submission use `SubmissionAuthority::mutex`;
- no `api_mutex` remains on the Vulkan data plane;
- device-loss handling, GPU completion, surface-slot and golden-render tests
  remain unchanged in semantics;
- validation-layer and multi-job scaling results are recorded after the final
  build/test pass.
