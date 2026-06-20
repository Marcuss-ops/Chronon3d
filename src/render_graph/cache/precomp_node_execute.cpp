// =============================================================================
// precomp_node_execute.cpp — Cached PrecompNode execution
//
/// Implements PrecompNode::execute() with SceneProgramCache integration.
///
/// The cache eliminates redundant graph rebuilds when the nested composition's
/// structure is unchanged across frames (only transforms/parameters differ).
///
/// Flow:
///   1. Calculate nested frame time (respecting m_start_frame / m_duration).
///   2. Evaluate the nested composition at the computed frame → Scene.
///   3. Compute SceneStructureKey from the scene (structure fingerprint +
///      active-set hash + render options hash + dimensions).
///   4. SceneProgramCache::find_or_compile():
///       - Cache HIT:  reuse the cached CompiledSceneProgram.
///       - Cache MISS: build the graph, compile, optimize, store.
///   5. Refresh per-frame payloads via refresh_compiled_graph_payloads().
///   6. Warm up FrameParameterBlock (sized to binding count).
///   7. Execute the cached program via the inner GraphExecutor.
// =============================================================================

#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/core/scene_hasher.hpp>
#include <chronon3d/render_graph/layer/layer_resolver.hpp>
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d::graph {

// ── Constructor ─────────────────────────────────────────────────────────────
//
PrecompNode::PrecompNode(std::string comp_name, Frame start_frame, Frame duration,
                         Frame cache_frame, size_t cache_capacity,
                         cache::TuneMode tune_mode, size_t tune_interval,
                         size_t tune_min_cap, size_t tune_max_cap)
    : RenderGraphNode(static_memory_cache("precomp"))
    , m_comp_name(std::move(comp_name))
    , m_full_name("Precomp:" + m_comp_name)
    , m_start_frame(start_frame)
    , m_duration(duration)
    , m_cache_frame(cache_frame)
    , m_cache(std::make_unique<cache::SceneProgramCache>(cache_capacity))
    , m_executor(std::make_unique<GraphExecutor>())
    , m_session()  // owns its own RenderSession
    , m_tune_mode(tune_mode)
    , m_tune_interval(tune_interval)
    , m_tune_min_cap(tune_min_cap)
    , m_tune_max_cap(tune_max_cap)
{
    // Forward evictions from the inner cache to the outer callback.
    m_cache->set_on_evict([this](const SceneStructureKey& key) {
        if (m_on_evict) m_on_evict(key);
    });

    // Apply auto-tuning configuration if enabled.
    if (m_tune_mode == cache::TuneMode::Auto) {
        cache::TuneConfig cfg;
        cfg.interval     = m_tune_interval;
        cfg.min_capacity = m_tune_min_cap;
        cfg.max_capacity = m_tune_max_cap;
        m_cache->set_tune_mode(cache::TuneMode::Auto);
        m_cache->set_tune_config(cfg);
        m_cache->set_log_label("ProgramCache[Precomp:" + m_comp_name + "]");
    }
}

PrecompNode::~PrecompNode() = default;

// ── execute() ───────────────────────────────────────────────────────────────
//
OwnedFB PrecompNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>)
{
    // ── 1. Guard: composition must exist in the registry ─────────────────
    if (!ctx.resources.registry || !ctx.resources.registry->contains(m_comp_name)) {
        return ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
    }

    // ── 2. Calculate nested frame time ───────────────────────────────────
    const Frame nested_frame = ctx.frame.frame - m_start_frame;
    if (nested_frame < 0 || (m_duration > 0 && nested_frame >= m_duration)) {
        return ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
    }

    // ── 3. Fetch nested composition & build nested context ───────────────
    const auto comp = ctx.resources.registry->create(m_comp_name);

    RenderGraphContext nested_ctx = ctx;
    nested_ctx.frame.frame   = nested_frame;
    nested_ctx.frame.width   = comp.width();
    nested_ctx.frame.height  = comp.height();
    nested_ctx.camera.camera = comp.camera;

    const Scene nested_scene = comp.evaluate(nested_frame);

    // ── 4. Compute SceneStructureKey for cache lookup ────────────────────
    SceneHasher hasher;
    SceneStructureKey key;
    key.topology_hash      = hasher.compute_structure_fingerprint(nested_scene);
    key.active_set_hash    = hasher.compute_active_at_fingerprint(nested_scene, nested_frame);
    key.render_options_hash = hash_combine(0, static_cast<uint64_t>(nested_ctx.options.ssaa_factor));
    key.width              = nested_ctx.frame.width;
    key.height             = nested_ctx.frame.height;
    key.ssaa_factor        = static_cast<int>(nested_ctx.options.ssaa_factor);

    // ── 5. Find or compile the nested program ────────────────────────────
    auto* program = m_cache->find_or_compile(key, [&]() -> std::unique_ptr<CompiledSceneProgram> {
        // Cache miss — delegate to the precomp_build factory (wired by graph_pipeline).
        if (!ctx.resources.precomp_build) {
            return nullptr;
        }
        return ctx.resources.precomp_build(nested_scene, nested_ctx);
    });

    if (!program || program->empty()) {
        // Empty / invalid program — return empty framebuffer.
        return ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
    }

    // ── 6. Warm up parameter block (no-op if size unchanged) ─────────────
    m_param_block.warm_up(program->bindings.size());

    // ── 7. Refresh per-frame payloads ────────────────────────────────────
    const auto resolved = detail::resolve_layers(nested_scene, nested_ctx);
    detail::refresh_compiled_graph_payloads(program->frame_graph, nested_scene, nested_ctx, resolved);

    // ── 8. Execute the cached program ────────────────────────────────────
    auto nested_result = m_executor->execute(program->frame_graph, nested_ctx, m_session);

    // ── 9. Auto-tune check (inside function scope) ───────────────────────
    // Run after every successful execution.  The check is lightweight
    // (counter increment + compare) and runs every frame.  The actual
    // auto_tune() call (which happens every m_tune_interval frames) may
    // call set_capacity() and evict excess entries, so it's better to
    // run it after the result is returned.
    if (nested_result) {
        if (m_tune_mode == cache::TuneMode::Auto) {
            ++m_tune_counter;
            if (m_tune_counter >= m_tune_interval) {
                m_tune_counter = 0;
                m_cache->auto_tune();
            }
        }
        return ctx.acquire_owned_fb(std::move(nested_result));
    }

    // Fallback: empty framebuffer.
    // Also run the auto-tune check on this path to ensure we don't miss
    // counting frames when the nested program is temporarily empty.
    if (m_tune_mode == cache::TuneMode::Auto) {
        ++m_tune_counter;
        if (m_tune_counter >= m_tune_interval) {
            m_tune_counter = 0;
            m_cache->auto_tune();
        }
    }
    return ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
}

} // namespace chronon3d::graph
