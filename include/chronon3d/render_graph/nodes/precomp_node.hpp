#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// precomp_node.hpp — PrecompNode (WP 5: identity-driven, thin executor)
//
// Renders a nested composition (Precomp layer).  All caching is delegated to
// the centralized SceneProgramStore on the parent session; the node
// itself holds only its composition identity (comp_name, frame range,
// cache frame), a `PrecompCachePolicy`, and a `FrameParameterBlock`.
//
// WP 5 PR-removals (now on RenderSession / SceneProgramStore via ctx):
//   - SceneProgramCache   → ctx.services.session->program_store().acquire(...)
//   - GraphExecutor        → borrowed from ctx.services.session->services.executor
//   - Auto-tune counter    → SceneProgramStore drives record_execution()
//                            every acquire() and the per-bucket
//                            cache auto_tunes at policy.tuning.interval.
//
// WP 5.1 — `instance_key()` is no longer a cached field; it derives from
// `ctx.node_exec.current_identity` at execute() time so the key carries
// the parent compiled graph's `GraphInstanceId` and THIS node's
// `StableNodeId`.  Two sibling PrecompNode instances running the same
// composition therefore produce DIFFERENT keys and map to distinct
// SceneProgramStore buckets (the WP 4.2 + 5.1 sibling-isolation invariant).
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
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

    /// Execute the nested composition.  Uses:
    ///   - ctx.services.session->program_store()    for cache lookup
    ///   - ctx.services.session->services.executor  for inner execution
    ///     (BORROWED — no local GraphExecutor instance)
    ///   - ctx.services.session->arena()            for inner graph PMR allocs
    OwnedFB execute(RenderGraphContext& ctx,
                    std::span<const FramebufferRef>,
                    std::span<const std::optional<raster::BBox>>) override;

    // ── Cache integration ────────────────────────────────────────────

    /// Register an eviction callback.  The callback is forwarded to the
    /// SceneProgramStore's per-instance cache lazily on the next
    /// acquire() (WP 5.3 — store owns mutable callback ownership; the
    /// node carries a copy only to forward through to the store on each
    /// call so the wiring survives across `clear()` + acquire() cycles).
    void set_on_evict(cache::ProgramEvictCallback cb);

    /// Compute the PrecompInstanceKey that identifies this node in the
    /// SceneProgramStore for the given execution context.
    ///
    /// WP 5.1 — derives from the executor-driven `current_identity` when
    /// the GraphExecutor has populated it (the production path); falls
    /// back to `kInvalid*Id` sentinel values when a test path drives
    /// `precomp.execute(...)` directly without going through the full
    /// executor.  In that fallback case the caller MUST override
    /// `ctx.node_exec.current_identity` before invoking execute() —
    /// otherwise the bucket key degenerates and multiple sibling
    /// precomps alias.
    [[nodiscard]] PrecompInstanceKey instance_key(
        const RenderGraphContext& ctx
    ) const noexcept;

    /// Backwards-compatible accessor used by the precomp_node_cache
    /// test fixture; returns the "would-be" instance key using the
    /// DEFAULT (invalid) identity.  Tests MUST pre-set
    /// `ctx.node_exec.current_identity` before invoking execute().
    [[nodiscard]] PrecompInstanceKey instance_key_default() const noexcept {
        return make_precomp_key(kInvalidGraphInstanceId, kInvalidStableNodeId);
    }

    /// Access the FrameParameterBlock (for diagnostics / testing).
    [[nodiscard]] FrameParameterBlock& param_block() { return m_param_block; }
    [[nodiscard]] const FrameParameterBlock& param_block() const { return m_param_block; }

private:
    std::string m_comp_name;
    std::string m_full_name;  // "Precomp:\" + m_comp_name, stored for string_view
    Frame m_start_frame{0};
    Frame m_duration{-1};
    Frame m_cache_frame{-1};

    // ── WP-5: only identity + policy + param block remain ──────────
    PrecompCachePolicy  m_cache_policy;
    FrameParameterBlock m_param_block;
    cache::ProgramEvictCallback m_on_evict;

    // Review #3 — track the per-instance bucket pointer that `m_on_evict`
    // was last forwarded to.  When the SceneProgramStore drops + recreates
    // the bucket (e.g. across a `RenderSession::reset_job()`), the next
    // acquire() yields a fresh `lease.bucket` that differs from this
    // pointer — the execute() path detects that and re-wires the callback
    // onto the new bucket.  Without this, a `clear()` would strand the
    // callback on a stale, dropped bucket.
    cache::SceneProgramCache* m_on_evict_wired_to{nullptr};
};

} // namespace chronon3d::graph
