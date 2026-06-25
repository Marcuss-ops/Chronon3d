# Work Package 8 — Remaining global-state and SDK work

## Current state

Completed:

- `RenderRuntime` owns a typed engine-local `AssetResolver`.
- Resolver behavior for absolute, relative, missing, mounted, and escaping paths is implemented and tested.
- Deep font/text/preflight consumers no longer call the removed one-argument string resolver.
- The legacy `AssetRegistry::resolve_path()` shortcut is removed.

The remaining problem is that deep consumers reach the typed resolver through process-global selection. `g_active_runtime`, `g_process_wide_assets_root`, `typed_resolver_for_deep_code()`, and the resolver mirror still allow one runtime to influence another. The public SDK facade also exposes software implementation types.

## TODO

### PR 8.0 — Pass the resolver explicitly

- [ ] Add `AssetResolver*` to the narrow render/session/context services that genuinely need path resolution.
- [ ] Pass the runtime-owned resolver into font loading, text rasterization, image loading, preflight, precomp construction, content helpers, CLI commands, and tests.
- [ ] Remove deep calls to `typed_resolver_for_deep_code()`.
- [ ] Do not replace the bridge with another singleton or thread-local active resolver.
- [ ] Keep the two-argument explicit-root helper only where the caller intentionally owns the root.

### PR 8.1 — Remove process-wide runtime and asset state

Remove:

- [ ] `g_active_runtime`
- [ ] `set_active_runtime()`
- [ ] `active_runtime()`
- [ ] `g_process_wide_assets_root`
- [ ] `set_process_wide_assets_root()`
- [ ] `process_wide_assets_root()`
- [ ] `default_assets_root_for_deep_code()`
- [ ] `typed_resolver_for_deep_code()`
- [ ] `g_deep_resolver_mirror`
- [ ] test-only global resolver reset hooks

Also:

- [ ] Stop `RenderRuntime::populate()` from publishing itself globally.
- [ ] Stop `RenderRuntime::set_default_assets_root()` from writing process state.
- [ ] Keep runtime destruction free from global-pointer cleanup.

### PR 8.2 — Add multi-engine isolation tests

- [ ] Construct two runtimes with different asset roots.
- [ ] Resolve the same relative path independently.
- [ ] Run font/text/preflight resolution concurrently for both runtimes.
- [ ] Destroy one runtime and verify the other remains valid.
- [ ] Verify CLI and tests work through explicit resolver wiring.
- [ ] Verify no last-created/last-mounted runtime changes another engine's result.

### PR 8.3 — Close the standard SDK facade

File:
- `include/chronon3d/api/render_engine.hpp`

Actions:

- [ ] Remove direct include of `software_renderer.hpp`.
- [ ] Remove direct exposure of software-session implementation types.
- [ ] Remove or relocate `renderer()`, `runtime()`, and `create_session()` from the standard facade.
- [ ] Move necessary expert access to an explicitly advanced/internal header.
- [ ] Keep ordinary render, settings, assets, and cache operations behind PIMPL.
- [ ] Update comments that still mention retired `ExecutionPlanCache` or process-wide asset fallback.

### PR 8.4 — Remove compatibility and contract leaks

- [ ] Remove `using RenderFrameInfo = FrameInput` after call-site migration.
- [ ] Replace `default_assets_root` string pointers in session services with explicit resolver access.
- [ ] Remove stale public comments describing Phase A global mirroring.
- [ ] Ensure public headers do not require backend implementation headers transitively.

### PR 8.5 — Add SDK and global-state guards

- [ ] Compile a consumer translation unit that includes only `api/render_engine.hpp`.
- [ ] Confirm the header does not include software renderer/session implementation headers.
- [ ] Build and run the standalone install consumer through `Chronon3D::SDK` only.
- [ ] Fail architecture checks if active-runtime or process-wide asset globals return.
- [ ] Fail architecture checks if the standard facade returns software implementation types.
- [ ] Verify advanced/internal headers are unnecessary for ordinary rendering.

## Dependencies

- Session resolver wiring should align with Work Package 3.
- Public facade cleanup should follow Work Package 7 backend separation.

## §9.4 Status — `graph_structure_unchanged` re-attached ✅ RESOLVED

The dormant `RenderPolicy::graph_structure_unchanged` flag was previously
noted as §9.4 "dormant, not closed" in the closure-note chain
(`dc914423` → `72d07d78` → `4a808e46`): the flag was written by
`src/render_graph/pipeline/scene.cpp` Phase 4 and read by the
coordinator-level `build_or_reuse_graph` in
`src/render_graph/pipeline/graph_cache_coordinator.cpp` (lines 91–127 —
the production fast-path), but it was **not** consulted by
`FrameGraphCompiler::compile` itself. After PR-2 rewire retired
`chronon3d::runtime::ExecutionPlanCache` (commit `9f9af90e`), the only
remaining `compile()`-internal consumer for this flag had gone away.

The §9.4 closure-note sub-sections that constrain the re-attachment:

- **Skip-safety constraints** (NIT-1, NIT-3): the skip is only sound
  when per-node `cache_policy` + `stable_node_id` are deterministically
  re-derivable from `graph + ctx.policy` alone, without per-call
  entropy; the skip must fall through to the full path on hash mismatch.
- **Cache-location neutrality** (NIT-2): where the prior
  `CompiledFrameGraph` lives is the API-consumer's choice; this ticket
  does NOT pick a storage home.
- **Affordance attribution**: `CompiledFrameGraph::structure_hash` is
  the candidate affordance reasoned backwards from §9.4's "stable
  fast-path" wording.

### Resolution (TICKET-008)

`FrameGraphCompiler::compile_with_reuse(graph, ctx, prior, options)`
(see TICKET-008 in `docs/FOLLOWUP_TICKETS.md`) re-attaches §9.4 inside the
compiler, with the contract that the closure-note specifies:

```cpp
const std::uint64_t current_hash = compute_structure_hash(graph, compiled.output);
const bool skip_heavy_phases =
    options.reuse_if_unchanged_predicate_safe()      // !run_optimizer gate
    && ctx.policy.graph_structure_unchanged           // runtime signal
    && prior_compiled.structure_hash == current_hash; // NIT-3 fall-through
```

When `skip_heavy_phases == true`, the three topology-derived
vectors (`levels`, `nodes`, `consumer_counts`) are deep-copied from
`prior_compiled` and the always-run post-conditions (graph move-in,
`compute_resource_lifetimes`, `structure_hash` refresh from the new
graph, `early_exit_skip` propagation from `ctx.node_exec`,
`graph_instance_id` re-derivation via FNV-1a, `validate_dag`,
`compiled.valid = true`) execute identically to the standard
`compile()` body — preserving every per-call invariant without
re-running the topo walk.

The skip path does NOT re-introduce `runtime::ExecutionPlanCache` (the
§9.4 closure-path criterion #1) — `compile_with_reuse` is a
compile-time affordance on the same `CompiledFrameGraph` payload,
not a parallel plan-cache layer.

**Known limitation documented inline** (compile_with_reuse doc-comment):
`compute_structure_hash` hashes node kind + input ids + output —
it does NOT hash node names + layer_ids. Two graphs with the same
topology but DIFFERENT node names produce the same
`structure_hash`, and the SKIP path returns the prior's `nodes[]`
array — whose `stable_node_id` field reflects the PRIOR names. The
`compiled.structure_hash` and `compiled.graph_instance_id` are
re-derived so the graph-level identity hash still reflects the new
names, but per-node `stable_node_id` is NOT. Callers that rely on
`compiled.nodes[id].stable_node_id` across the reuse boundary must
keep node names stable OR fall through by leaving
`graph_structure_unchanged=false` when names change.

**Status**: ✅ **RESOLVED** — TICKET-008 closed via the
`compile_with_reuse` overload. The five acceptance tests (Test A–E)
in `tests/render_graph/compiler/test_frame_graph_compiler.cpp` cover
the skip path, the three fall-through modes, and the always-run
post-condition invariants.



- [ ] Asset resolution is explicit and engine-local end to end.
- [ ] No active-runtime, process-wide-root, or global typed-resolver bridge remains.
- [ ] Two runtimes cannot contaminate each other.
- [ ] Standard SDK headers expose no software implementation types.
- [ ] Ordinary consumers build and run through `Chronon3D::SDK` only.
