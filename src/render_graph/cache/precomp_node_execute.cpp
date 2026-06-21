// =============================================================================
// precomp_node_execute.cpp — PrecompNode::execute() (PR-5: thin-node refactor)
//
// PR-5 — PrecompNode no longer owns SceneProgramCache, GraphExecutor,
// ExecutionPlanCache, RenderSession, or auto-tune state.  All of those are
// now centralised on the parent RenderSession (accessed via
// `ctx.services.session`).
//
// Flow:
//   1. Calculate nested frame time.
//   2. Evaluate nested composition → Scene.
//   3. Compute SceneStructureKey.
//   4. ctx.services.session->program_store->acquire(instance_key(), ...)
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
    , m_instance_key(PrecompInstanceKey{
          .graph = hash_string(m_comp_name),
          .node  = 0})
{
}

PrecompNode::~PrecompNode() = default;

// ── instance_key() ──────────────────────────────────────────────────────────

PrecompInstanceKey PrecompNode::instance_key() const {
    return m_instance_key;
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
    auto lease = session->program_store->acquire(
        m_instance_key, key, m_cache_policy,
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
        session->program_store->set_on_evict(m_instance_key, m_on_evict);
    }

    auto* program = lease.program;
    if (!program || program->empty()) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    // ── 6. Warm up parameter block (no-op if size unchanged) ─────────────
    m_param_block.warm_up(program->bindings.size());

    // ── 7. Refresh per-frame payloads ────────────────────────────────────
    const auto resolved = detail::resolve_layers(nested_scene, nested_ctx);
    detail::refresh_compiled_graph_payloads(program->frame_graph, nested_scene, nested_ctx, resolved);

    // ── 8. Execute the cached program ────────────────────────────────────
    // PR-5 + PR-2 rewire (WP-8 followup) — GraphExecutor is stateless;
    // the topology plan lives on `program->frame_graph.levels`, so we
    // was RETIRED alongside the legacy RenderGraph& overloads).
    GraphExecutor local_executor;

    auto nested_result = local_executor.execute(
        program->frame_graph, nested_ctx, *session,
        *ctx.services.scheduler);

    if (nested_result) {
        return ctx.acquire_owned_fb(std::move(nested_result));
    }

    return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
}

} // namespace chronon3d::graph
