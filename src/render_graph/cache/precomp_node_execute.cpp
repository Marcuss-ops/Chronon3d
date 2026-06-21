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
// Flow:
//   1. Calculate nested frame time.
//   2. Evaluate nested composition → Scene.
//   3. Compute SceneStructureKey.
//   4. ctx.services.session->program_store().acquire(instance_key(), ...)
//      → cache HIT: reuse; cache MISS: compile via PrecompBuilderService.
//   5. Refresh per-frame payloads, warm up param block.
//   6. Execute via session's executor + scheduler (topology plans live
//      on the cached CompiledFrameGraph::levels; see WP-8 followup).
//
// The comp_name-based instance key ensures each PrecompNode gets its own
// partition in the shared SceneProgramStore.
// =============================================================================

#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>  // WP 5.2 — ProgramLease holds shared_ptr<CompiledSceneProgram>; member access (`.empty()`, `.bindings`, `.frame_graph`) requires complete type
#include <chronon3d/render_graph/core/scene_hasher.hpp>
#include <chronon3d/render_graph/layer/layer_resolver.hpp>
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include <chronon3d/render_graph/builder/precomp_builder_service.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>

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

// ── execute() ───────────────────────────────────────────────────────────────

OwnedFB PrecompNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>)
{
    // ── 0. Guard: parent session must be wired ─────────────────────────
    auto* session = ctx.services.session;
    if (!session) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 1. Guard: composition must exist in the registry ─────────────────
    if (!ctx.services.registry || !ctx.services.registry->contains(m_comp_name)) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 2. Calculate nested frame time ───────────────────────────────────
    const Frame nested_frame = ctx.frame_input.frame - m_start_frame;
    if (nested_frame < 0 || (m_duration > 0 && nested_frame >= m_duration)) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 3. Fetch nested composition & build nested context ───────────────
    const auto comp = ctx.services.registry->create(m_comp_name);

    RenderGraphContext nested_ctx = ctx;
    nested_ctx.frame_input.frame   = nested_frame;
    nested_ctx.frame_input.width   = comp.width();
    nested_ctx.frame_input.height  = comp.height();
    nested_ctx.frame_input.camera = comp.camera;

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

    // ── 5. Acquire program from the centralized store ────────────────────
    auto lease = session->program_store().acquire(
        instance_key(ctx), key, m_cache_policy,
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
        session->program_store().set_on_evict(instance_key(ctx), m_on_evict);
    }

    // WP 5.2 — ProgramLease::program is std::shared_ptr<CompiledSceneProgram>;
    // we consume it via `.get()` to obtain a non-owning raw pointer for the
    // duration of this execute() call.  The shared_ptr keeps the program
    // alive even if a concurrent `clear()` on the store evicts its primary
    // reference (lifetime invariant documented in scene_program_store.hpp).
    auto* program = lease.program.get();
    if (!program || program->empty()) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 6. Warm up parameter block (no-op if size unchanged) ─────────────
    m_param_block.warm_up(program->bindings.size());

    // ── 7. Refresh per-frame payloads ────────────────────────────────────
    const auto resolved = detail::resolve_layers(nested_scene, nested_ctx);
    detail::refresh_compiled_graph_payloads(program->frame_graph, nested_scene, nested_ctx, resolved);

    // ── 8. Execute the cached program (WP-5 + WP-0 PR 0.1) ──────────────
    // PR-5 — GraphExecutor is stateless; the topology plan lives on
    // `program->frame_graph.levels` (no legacy RenderGraph& overloads).
    // WP-0 PR 0.1 — PrecompNode MUST NOT construct a local GraphExecutor
    // inline; borrow `session->services.executor` from the parent
    // SessionServices table.  This eliminates a per-frame
    // GraphExecutor ctor + dtor pair on every nested precomp execution
    // and centralises scheduler binding on the runtime-owned executor.
    // Regression guard: `tools/check_architecture_boundaries.sh` check
    // [9/12] blocks reintroduction of `GraphExecutor <name>;`
    // declarations in this TU.
    const auto* inner_executor = session->services.executor;
    if (!inner_executor) {
        return ctx.acquire_owned_fb(
            ctx.frame_input.width, ctx.frame_input.height);
    }
    auto nested_result = inner_executor->execute(
        program->frame_graph, nested_ctx, *session,
        *ctx.services.scheduler);

    if (nested_result) {
        return ctx.acquire_owned_fb(std::move(nested_result));
    }

    return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
}

} // namespace chronon3d::graph
