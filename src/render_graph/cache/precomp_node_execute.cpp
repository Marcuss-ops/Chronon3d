// =============================================================================
// precomp_node_execute.cpp — PrecompNode::execute() (PR-5: thin-node refactor)
//
// PR-5 — PrecompNode no longer owns SceneProgramCache, GraphExecutor,
// topological-plan cache (retired as `runtime::ExecutionPlanCache` in
// docs/CHANGELOG.md R6), RenderSession, or auto-tune state.  All of those
// are now centralised on the parent RenderSession / CompiledFrameGraph
// (accessed via `ctx.services.session` for the program_store, and via
// `session->services().executor` for the borrowed GraphExecutor).
//
// WP-6 PR 6.3 — Body consolidation.  Legacy `OwnedFB execute(ctx, ...)`
// is a 13-line wrapper that synthesises a Root `ExecutionScope` from
// the borrowed session and delegates to `execute_with_scope(parent,
// ...)` — the SINGLE canonical body that owns the lease-held + child-
// scope + execute_with_scope dispatch contract documented in
// `docs/03-execution-scope-and-precomp.md` §4.3.  This eliminates the
// previous 85-line duplication between the two methods where any
// reviewer-flagged concern (lease lifetime, scope-chain construction,
// eviction callback wiring, owner_key derivation, bail-out check) had
// to be addressed in both bodies.
//
// Flow (single canonical body in `execute_with_scope`):
//   1. Calculate nested frame time.
//   2. Evaluate nested composition → Scene.
//   3. Compute SceneStructureKey.
//   4. ctx.services.session->program_store().acquire(precomp_key, ...)
//      → cache HIT: reuse; cache MISS: compile via PrecompBuilderService.
//   5. Refresh per-frame payloads, warm up param block.
//   6. Construct child Precomp ExecutionScope + FrameArena distinct from
//      the parent (PR 6.3 exit criterion #1); set owner_key on the
//      child scope (PR 6.5 anti-recursion contract).
//   7. would_recurse / would_overflow bail-out per docs/03-§4.4.
//   8. Inner execute_with_scope(precomp_scope, ...) — lease's
//      shared_ptr keeps program alive through the call (RAII order).
// =============================================================================

#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>  // WP 5.2 — ProgramLease holds shared_ptr<CompiledSceneProgram>; member access (`.empty()`, `.bindings`, `.frame_graph`) requires complete type
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/render_graph/layer/layer_resolver.hpp>
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include <chronon3d/render_graph/builder/precomp_builder_service.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/execution/execution_scope_types.hpp>

namespace chronon3d::graph {

// ── Constructor ─────────────────────────────────────────────────────────────

PrecompNode::PrecompNode(std::string comp_name, Frame start_frame, Frame duration,
                         Frame cache_frame, PrecompCachePolicy cache_policy)
    : RenderGraphNode(static_memory_cache("precomp"))
    , m_comp_name(std::move(comp_name))
    , m_full_name("Precomp:" + m_comp_name)
    , m_start_frame(start_frame)
    , m_duration(duration)
    , m_cache_frame(cache_frame)
    , m_cache_policy(std::move(cache_policy))
{
    // WP 5.1 — the per-instance `PrecompInstanceKey` is no longer a
    // cached member field.  It is derived per-call from the executing
    // node's `current_identity` (set by `GraphExecutor::execute_single_node`
    // immediately before delegating to `node->execute(...)`) via
    // `instance_key(ctx)`.  This is what makes two sibling PrecompNode
    // instances running the same composition map to DISTINCT cache
    // buckets (WP 4.2 + WP 5.1 invariant).
}

PrecompNode::~PrecompNode() = default;

// ── instance_key() ──────────────────────────────────────────────────────────

PrecompInstanceKey PrecompNode::instance_key(const RenderGraphContext& ctx) const noexcept {
    // WP 5.1 — derive the owner key from the executor-driven
    // `current_identity` on the executing node's context when the
    // GraphExecutor has populated it (production path).  When a test
    // path drives `precomp.execute(...)` directly without going through
    // the full executor, `current_identity` stays at its default
    // (invalid sentinel) and we fall back to `instance_key_default()`
    // so per-instance buckets still partition correctly.  In that
    // fallback case the caller MAY override `ctx.node_exec.current_identity`
    // before invoking execute() — sibling PrecompNodes driving the
    // fixture without an explicit override WILL alias on the invalid
    // key (acceptable for tests that don't assert sibling-isolation).
    const auto& ident = ctx.node_exec.current_identity;
    if (ident.valid()) {
        return make_precomp_key(ident.graph, ident.node);
    }
    return instance_key_default();
}

// ── set_on_evict() ──────────────────────────────────────────────────────────

void PrecompNode::set_on_evict(cache::ProgramEvictCallback cb) {
    m_on_evict = std::move(cb);
    // Note: the callback is forwarded to the store's per-instance cache
    // lazily on the next acquire().  If the store hasn't created the
    // instance yet, the callback is queued here and will be registered
    // when acquire() first creates the per-instance cache.
}

// ── execute() — WP-6 PR 6.3 thin delegating wrapper ─────────────────────────
//
// Legacy back-compat surface (kept as `override` so existing test lattice
// compiles unmodified).  Synthesises a Root `ExecutionScope` from the
// borrowed session + `precomp_key.graph` (sourcing graph identity from
// the SAME single source-of-truth used inside the canonical body so
// the two surfaces agree bit-for-bit) and delegates to
// `execute_with_scope(parent_root, ctx, ...)` — the SINGLE canonical
// body below.
//
// Until WP-6 PR 6.2 ("Add the root scope") wires the producer-side
// root scope into `GraphExecutor::execute_single_node`, production
// code flows through THIS legacy `execute` entry which then
// synthesises the parent internally.  Post-PR-6.2, the production
// path will invoke `node->execute_with_scope(root_scope, ...)` directly
// — at that point this wrapper exists only as back-compat.
//
// §0 guard for `session == nullptr` is the only divergence from
// `execute_with_scope` because the canonical body takes `parent`
// (which already enforces lifetime via the borrowed session ref).
NodeExecResult PrecompNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> fbs,
    std::span<const std::optional<raster::BBox>> clips)
{
    auto* session = ctx.services.session;
    if (!session) {
        return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height)};
    }
    const auto precomp_key = instance_key(ctx);
    const auto graph_id =
        static_cast<GraphInstanceId>(precomp_key.graph);
    // FASE 5 closed ExecutionScope's public direct-ctor surface; the
    // synthesised Root must route through make_root() with an explicit
    // FrameArena — borrow the same session's arena so the parent chain
    // anchor is consistent with execute_with_scope() (the canonical body).
    ExecutionScope parent_root = ExecutionScope::make_root(
        *session, session->arena(), graph_id);
    return execute_with_scope(parent_root, ctx, fbs, clips);
}

// ── execute_with_scope() — WP-6 PR 6.3 SINGLE canonical body ────────────────
//
// Production callers (GraphExecutor::execute_single_node post-PR-6.2
// wiring, tile orchestrator post-PR-6.4 propagating tile_scope as
// parent, future diagnostic commands) invoke this overload directly.
// The `parent` reference is BORROWED (lifetime contract documented
// in `execution_scope.hpp`); the `FrameArena child_arena` is
// stack-local to this function frame.
NodeExecResult PrecompNode::execute_with_scope(
    ExecutionScope& parent,
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>)
{
    // ── 0. Guard: parent session must be wired (via the borrowed parent ref)
    auto& session = parent.session();
    if (!ctx.services.registry || !ctx.services.registry->contains(m_comp_name)) {
        return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height)};
    }

    // ── 1. Calculate nested frame time ───────────────────────────────────
    const Frame nested_frame = ctx.frame_input.frame - m_start_frame;
    if (nested_frame < 0 || (m_duration > 0 && nested_frame >= m_duration)) {
        return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height)};
    }

    // ── 2. Hoisted PrecompInstanceKey — single computation reused by §3
    //      (lease acquire), §3b (set_on_evict), §6 (parent_root graph id,
    //      precomp_scope graph id — owner_key is applied via make_child
    //      arg in §9-§8).  Eliminates 3 redundant `instance_key(ctx)`
    //      walks in the previous version.
    const auto precomp_key = instance_key(ctx);

    // ── 3. Fetch nested composition & build nested context ──────────────
    const auto comp = ctx.services.registry->create(m_comp_name);

    RenderGraphContext nested_ctx = ctx;
    nested_ctx.frame_input.frame   = nested_frame;
    nested_ctx.frame_input.width   = comp.width();
    nested_ctx.frame_input.height  = comp.height();
    nested_ctx.frame_input.camera = comp.camera;
    nested_ctx.node_exec.current_identity.graph = static_cast<GraphInstanceId>(precomp_key.graph);

    const Scene nested_scene = comp.evaluate(nested_frame);

    // ── 4. Compute SceneStructureKey for cache lookup ────────────────────
    SceneHasher hasher;
    SceneStructureKey key;
    key.topology_hash      = hasher.compute_structure_fingerprint(nested_scene);
    key.active_set_hash    = hasher.compute_active_at_fingerprint(nested_scene, nested_frame);
    key.render_options_hash = hash_combine(0, static_cast<uint64_t>(nested_ctx.policy.ssaa_factor));
    key.width              = nested_ctx.frame_input.width;
    key.height             = nested_ctx.frame_input.height;
    key.ssaa_factor        = static_cast<int>(nested_ctx.policy.ssaa_factor);

    // ── 5. Acquire program from the centralized store (lease held for
    //      the entire function frame — RAII reverse-decl-order keeps
    //      `lease`'s shared_ptr<CompiledSceneProgram> alive past the
    //      `execute_with_scope` call below + the function's tail return).
    auto lease = session.program_store().acquire(
        precomp_key, key, m_cache_policy,
        [&]() -> std::unique_ptr<CompiledSceneProgram> {
            // Cache miss — delegate to the typed PrecompBuilderService.
            if (!ctx.services.precomp_builder) {
                return nullptr;
            }
            return ctx.services.precomp_builder->build(nested_scene, nested_ctx);
        });

    // Forward eviction callback to the store's per-instance cache if one
    // is set and the instance was just created (or on first call).
    if (m_on_evict) {
        session.program_store().set_on_evict(precomp_key, m_on_evict);
    }

    // WP 5.2 — ProgramLease::program is std::shared_ptr<CompiledSceneProgram>;
    // we consume it via `.get()` to obtain a non-owning raw pointer for the
    // duration of this execute_with_scope() call.  The shared_ptr keeps the
    // program alive even if a concurrent `clear()` on the store evicts its
    // primary reference (lifetime invariant documented in
    // scene_program_store.hpp).
    auto* program = lease.program.get();
    if (!program || program->empty()) {
        return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height)};
    }

    // ── 6. Warm up parameter block (no-op if size unchanged) ─────────────
    m_param_block.warm_up(program->bindings.size());

    // ── 7. Refresh per-frame payloads ────────────────────────────────────
    const auto resolved = detail::resolve_layers(nested_scene, nested_ctx);
    detail::refresh_compiled_graph_payloads(program->frame_graph, nested_scene, nested_ctx, resolved);

    // ── 8. §9.3 — Construct child Precomp ExecutionScope via make_child()
    //
    // make_child() validates all invariants (non-Root kind, parent not null,
    // arena distinct from parent, chain limit, recursion check) and returns
    // Result<ExecutionScope, ScopeError>.  On failure, bail out with empty fb.
    FrameArena child_arena;
    const auto graph_id =
        static_cast<GraphInstanceId>(precomp_key.graph);
    auto precomp_result = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, child_arena,
        graph_id, &parent, precomp_key.node);

    if (!precomp_result) {
        return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height)};
    }
    ExecutionScope& precomp_scope = precomp_result.value();

    // ── 9. Execute the cached program (WP-5 + WP-0 PR 0.1 + WP-6 PR 6.3) ─
    // PR-5 — GraphExecutor is stateless; the topology plan lives on
    // `program->frame_graph.levels` (no legacy RenderGraph& overloads).
    // WP-0 PR 0.1 — PrecompNode MUST NOT construct a local GraphExecutor
    // inline; borrow `session.services.executor` from the parent
    // SessionServices table.  Regression guard:
    // `tools/check_architecture_boundaries.sh` check [9/12] blocks
    // reintroduction of `GraphExecutor <name>;` declarations in this TU.
    // WP-6 PR 6.3 — line-139-area legacy `inner_executor->execute(...,
    // *session, ...)` is replaced by `inner_executor->execute_with_scope(
    // precomp_scope, ...)`.  Lease's `shared_ptr<CompiledSceneProgram>`
    // keeps the program alive throughout the call (RAII reverse-decl-
    // order means lease destructed LAST in this function frame).
    const auto* inner_executor = session.services.executor;
    if (!inner_executor) {
        return NodeExecResult{ctx.acquire_owned_fb(
            ctx.frame_input.width, ctx.frame_input.height)};
    }
    auto nested_result = inner_executor->execute_with_scope(
        program->frame_graph, nested_ctx, precomp_scope,
        *ctx.services.scheduler);

    if (nested_result) {
        return NodeExecResult{ctx.acquire_owned_fb(std::move(nested_result))};
    }

    return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height)};
}

} // namespace chronon3d::graph
