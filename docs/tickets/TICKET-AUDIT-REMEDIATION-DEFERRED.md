# TICKET-AUDIT-REMEDIATION-DEFERRED — audit findings deferred pending certification

| **TICKET-AUDIT-REMEDIATION-DEFERRED** | |
| --- | --- |
| **Stato** | OPEN (deferred by design, each with a named precondition) |
| **Priorità** | P1 (F8) / P2 (F12, F13, F14) |
| **Problema** | Four findings of the technical audit are real but not safely fixable by a local refactor: each would move a synchronisation invariant or an ownership/key lifetime that only the existing certification lanes can prove intact. Touching them without that evidence trades a measured cost for an unmeasured regression. They are recorded here so the decision is explicit rather than silently dropped. |

Everything else in the audit was fixed in-tree; see the individual commits. This
ticket is the honest residue.

---

## F8 — double nested lock per batched surface operation — **DEFERRED (P1)**

**Evidence.** `src/backends/vulkan/vulkan_backend_impl.hpp:369-391` —
`pin_recording_leases_locked` acquires `recording_leases_mutex` while the caller
holds the per-store `DomainLock` (`release_recording_leases` at `:394-397` and
`release_lease_entry_locked` at `:411-420` show the same nesting order);
`vulkan_surface_authority.hpp:38` uses one store mutex for bindings +
physical surfaces + pins + aliases. Every batched surface op therefore takes two
nested locks, and the store lock serialises all surface ops of a plan.

**Why deferred.** The two-lock split is what protects the pin/lease invariant,
including the rollback at `:376-386`. Consolidating the locks, or sharding the store
mutex, changes the ordering guarantees between pin, lease and surface materialisation.
The audit itself rates the risk **High** and requires the N4 regression test as the
guard.

**Precondition to start.** A green run of the N4 surface/lease regression suite on the
build host, plus a before/after record of `phase4-framebuffer-telemetry` style lock-wait
vs lock-hold measurements showing the nested acquisition is actually material. Without
the measurement the change is speculative.

## F12 — string-keyed maps per frame on the dirty/refresh path — **DEFERRED (P2)**

**Evidence.** `src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:210` builds
`unordered_map<std::string, LayerBBoxState>` per frame (one key materialisation per
active layer); `include/chronon3d/runtime/dirty_history.hpp:144` keeps
`previous_layers` as the same string-keyed type and is diffed every frame
(`scene_dirty.cpp` reads `previous_layers.find(name)`); the refresh lanes
(`source.cpp`, `multi_source.cpp`, `scene_refresh.cpp`) do string-keyed
`is_static_cache` lookups per layer.

**Landed (safe subset).** The refresh lanes no longer hash the layer name twice per
layer: `refresh/source.cpp`, `refresh/multi_source.cpp` and `scene_refresh.cpp`
replaced the `is_static_cache.count(k) ? is_static_cache.at(k) : fallback` pair (two
hashes) with a single `find()` (one hash), keeping the fallback bit-identical. The
collector merge in `layer_bbox_collector.cpp` already materialises each layer key
exactly once (data-oriented `PendingLayerBBox` TLS), so no further allocation win is
available there without changing the caller contract.

**Why the rest is deferred.** The audit's suggestion — index by `resolved_index` or key by
`string_view` — changes the *identity* used across frames. `resolved_index` is a
per-frame ordering, not a stable identity, and a `string_view` key would dangle as soon
as the topology cache (see F11) evicts or replaces an entry, because
`commit_prev_frame_state` moves a complete map between frames. A dangling key here reads
as a spurious dirty rect and is exactly the kind of bug that surfaces as a pixel diff,
not as a crash.

**Precondition to start.** Pixel-parity evidence: the render-graph golden suite plus the
dirty-rect determinism corpus must be runnable on the build host, and the change must
come with an interned, cross-frame-stable layer identity (not a per-frame index) so the
diff key cannot outlive its storage.

## F13 — motion blur allocates the full cache/session domain per frame — **DEFERRED (P2)**

**Evidence.** `src/render_graph/pipeline/composition.cpp:309-321` builds five
`unique_ptr` vectors (`NodeCache`, `CompiledGraphCache`, `JobSession`,
`TransformScratch`, `FramebufferPool`) per frame while motion blur is active, one per
temporal sample.

**Why deferred — deliberate.** Per-sample isolation is the *determinism guarantee* of
the temporal sample, and the in-place comment says so. The budget authority
(`max_temporal_pixels`) bounds the cost. Pooling any of these objects reintroduces
cross-sample state sharing.

**Precondition to start.** A pooling design certified against the determinism matrix
(1/4/8 workers, byte-identical outputs) plus the motion-blur golden corpus. Until then
this stays as designed.

## F14 — `make_shared<Framebuffer>` in the non-pooled acquire fallbacks — **DEFERRED (P2)**

**Evidence.** `src/render_graph/framebuffer_acquire_shared_detail.hpp` — four sites:
the terminal native-video encode-surface wrapper (`:31`), the `require_native_gpu`
wrapper (`:66`) and the two `framebuffer_pool == nullptr` fallbacks (`:91`, `:153`).

**Why deferred.** These are the paths where the framebuffer is genuinely owned by the
returned `shared_ptr` (the pool, when present, is the owner on every other path). Removing
the control-block allocation means pooling the *Framebuffer wrapper objects* themselves —
a new residency primitive, which AGENTS.md requires to arrive with an ADR and a cache-taxonomy
placement, not as a drive-by refactor. The pool-absent fallbacks are also a degraded
configuration, already visible through the pool statistics rather than a steady-state
cost.

**Precondition to start.** An ADR naming the primitive's cache family (ResidencyCache) and
its owner, plus a baseline showing the wrapper allocations are actually material on the
`require_native_gpu` lane (the steady-state allocation gate already covers the pooled path).
