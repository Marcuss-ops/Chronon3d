#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// precomp_node.hpp — PrecompNode (PR-5: thin identity-only node)
//
// Renders a nested composition (Precomp layer).  All caching is delegated to
// the centralized SceneProgramStore on the parent RenderSession; the node
// itself holds only identity (comp_name, frame range, cache frame) +
// a PrecompCachePolicy + a FrameParameterBlock.
//
// PR-5 removals (now on RenderSession / SceneProgramStore):
//   - SceneProgramCache   → ctx.services.session->program_store->acquire(...)
//   - GraphExecutor        → ctx.services.session->services.executor
//   - ExecutionPlanCache   → ctx.services.session->services.plan_cache
//   - RenderSession        → ctx.services.session (borrowed for inner exec)
//   - Auto-tune counter    → SceneProgramStore handles per-instance auto-tune
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_block.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <span>

namespace chronon3d::graph {

class PrecompNode final : public RenderGraphNode {
public:
    /// Construct a PrecompNode for the named composition.
    /// @param comp_name     Name of the composition in the registry.
    /// @param start_frame   Frame offset where this precomp starts.
    /// @param duration      Number of frames (0 or negative = unlimited).
    /// @param cache_frame   Frame used for node-cache key.
    /// @param cache_policy  Per-instance cache policy forwarded to the
    ///                      SceneProgramStore on first acquire().
    PrecompNode(std::string comp_name, Frame start_frame, Frame duration,
                Frame cache_frame = Frame{-1},
                PrecompCachePolicy cache_policy = {});

    ~PrecompNode() override;

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Precomp; }
    std::string_view name() const noexcept override { return m_full_name; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "precomp",
            .frame = m_cache_frame >= 0 ? m_cache_frame : Frame{0},
            .width  = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_string(m_comp_name)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    /// Execute the nested composition.  Uses ctx.services.session for:
    ///   - program_store->acquire() for cache lookup
    ///   - arena() for inner graph PMR allocations
    ///   - services.executor / services.plan_cache for inner execution
    OwnedFB execute(RenderGraphContext& ctx,
                    std::span<const FramebufferRef>,
                    std::span<const std::optional<raster::BBox>>) override;

    // ── Cache integration ────────────────────────────────────────────

    /// Register an eviction callback.  Forwarded to the SceneProgramStore
    /// for this instance (keyed by instance_key()).
    void set_on_evict(cache::ProgramEvictCallback cb);

    /// Compute the PrecompInstanceKey that identifies this node in the
    /// SceneProgramStore.  Stable across frames for the same composition.
    [[nodiscard]] PrecompInstanceKey instance_key() const;

    /// Access the FrameParameterBlock (for diagnostics / testing).
    [[nodiscard]] FrameParameterBlock& param_block() { return m_param_block; }
    [[nodiscard]] const FrameParameterBlock& param_block() const { return m_param_block; }

private:
    std::string m_comp_name;
    std::string m_full_name;  // "Precomp:" + m_comp_name, stored for string_view
    Frame m_start_frame{0};
    Frame m_duration{-1};
    Frame m_cache_frame{-1};

    // ── PR-5: only identity + policy + param block remain ──────────
    PrecompCachePolicy  m_cache_policy;
    FrameParameterBlock m_param_block;
    cache::ProgramEvictCallback m_on_evict;

    // ── Cached instance key (computed once from m_comp_name) ───────
    PrecompInstanceKey  m_instance_key;
};

} // namespace chronon3d::graph
