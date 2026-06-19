#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// precomp_node.hpp — PrecompNode with SceneProgramCache integration
//
// Renders a nested composition (Precomp layer) by building/compiling a
// render graph for the inner scene and caching the compiled program via
// SceneProgramCache.  On cache hit, only per-frame payload refresh is
// needed — no full graph rebuild.  Supports eviction callbacks so outer
// caches can invalidate nested state.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/core/memory/render_session.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_block.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <span>

namespace chronon3d::graph {

class PrecompNode final : public RenderGraphNode {
public:
    /// Construct a PrecompNode for the named composition.
    /// @param cache_capacity  Size of the inner SceneProgramCache (default 8).
    /// @param tune_mode       Auto-tuning mode for the cache (default Fixed).
    /// @param tune_interval   How many execute() calls between auto-tune checks.
    /// @param tune_min_cap    Minimum cache capacity when down-tuning.
    /// @param tune_max_cap    Maximum cache capacity when up-tuning.
    PrecompNode(std::string comp_name, Frame start_frame, Frame duration,
                Frame cache_frame = Frame{-1},
                size_t cache_capacity = 8,
                cache::TuneMode tune_mode = cache::TuneMode::Fixed,
                size_t tune_interval = 30,
                size_t tune_min_cap = 2,
                size_t tune_max_cap = 128);
    ~PrecompNode() override;

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Precomp; }
    std::string_view name() const noexcept override { return m_full_name; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "precomp",
            .frame = m_cache_frame >= 0
                ? m_cache_frame
                : (cache_policy().frame_dependent ? (ctx.frame.frame - m_start_frame) : Frame{0}),
            .width  = ctx.frame.width,
            .height = ctx.frame.height,
            .params_hash = hash_string(m_comp_name)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
        return raster::BBox{0, 0, ctx.frame.width, ctx.frame.height};
    }

    /// Execute the nested composition, using the inner cache to avoid
    /// rebuilding the graph when the scene structure hasn't changed.
    OwnedFB execute(RenderGraphContext& ctx,
                    std::span<const FramebufferRef>,
                    std::span<const std::optional<raster::BBox>>) override;

    // ── Cache integration ────────────────────────────────────────────

    /// Register an eviction callback forwarded from the inner
    /// SceneProgramCache.  Invoked whenever the inner cache evicts an entry
    /// (e.g. due to LRU eviction or explicit erase).
    void set_on_evict(cache::ProgramEvictCallback cb) { m_on_evict = std::move(cb); }

    /// Access the inner SceneProgramCache (for diagnostics / testing).
    [[nodiscard]] cache::SceneProgramCache& inner_cache() { return *m_cache; }
    [[nodiscard]] const cache::SceneProgramCache& inner_cache() const { return *m_cache; }

    /// Access the FrameParameterBlock (for diagnostics / testing).
    [[nodiscard]] FrameParameterBlock& param_block() { return m_param_block; }
    [[nodiscard]] const FrameParameterBlock& param_block() const { return m_param_block; }

    /// Invalidate a cached entry for the given key (called when an outer
    /// cache evicts this precomp's parent program).
    void invalidate_entry(const SceneStructureKey& key) { m_cache->erase(key); }

private:
    std::string m_comp_name;
    std::string m_full_name;  // "Precomp:" + m_comp_name, stored for string_view
    Frame m_start_frame{0};
    Frame m_duration{-1};
    Frame m_cache_frame{-1};

    // ── Caching members ────────────────────────────────────────────
    std::unique_ptr<cache::SceneProgramCache> m_cache;
    FrameParameterBlock m_param_block;
    std::unique_ptr<GraphExecutor> m_executor;
    RenderSession m_session;  // per-session state (arena + frame history)
    cache::ProgramEvictCallback m_on_evict;

    // ── Auto-tuning state ─────────────────────────────────────────────
    cache::TuneMode m_tune_mode{cache::TuneMode::Fixed};
    size_t m_tune_interval{30};
    size_t m_tune_counter{0};
    size_t m_tune_min_cap{2};
    size_t m_tune_max_cap{128};
};

} // namespace chronon3d::graph
