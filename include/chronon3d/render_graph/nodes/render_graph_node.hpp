#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/core/cache_policy.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/math/projection_context.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <span>

namespace chronon3d {
    class CompositionRegistry;
    struct RenderCounters;
}

namespace chronon3d::video {
    class MediaFrameProvider;
}

namespace chronon3d::graph {

class RenderBackend;
using GraphNodeId = uint32_t;

enum class RenderGraphNodeKind {
    Source,
    Mask,
    Effect,
    Transform,
    Composite,
    Precomp,
    Video,
    Adjustment,
    MotionBlur,
    ColorConvert,
    TrackMatte,
    Output,
    Transition,
    ClipTransition,
    TextRun
};

[[nodiscard]] inline std::string_view to_string(RenderGraphNodeKind kind) {
    return enum_utils::enum_name_exact(kind);
}

class RenderGraphNode {
public:
    explicit RenderGraphNode(RenderNodeCachePolicy p = frame_variant_cache("default"))
        : m_cache_policy(p) {}

    virtual ~RenderGraphNode() = default;

    [[nodiscard]] virtual std::optional<raster::BBox> predicted_bbox(const RenderGraphContext& ctx) const {
        return std::nullopt;
    }
    [[nodiscard]] virtual std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) const {
        (void)input_bboxes;
        return predicted_bbox(ctx);
    }

    virtual RenderGraphNodeKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Explicit history/preroll contract. Frame-local nodes default to zero.
    /// MotionBlur is intrinsically temporal, so the base contract guarantees
    /// at least one history frame instead of allowing a hidden state dependency.
    /// Specialized temporal nodes may override with larger frame/duration windows.
    [[nodiscard]] virtual TemporalRequirements temporal_requirements() const noexcept {
        if (kind() == RenderGraphNodeKind::MotionBlur) {
            return TemporalRequirements{.history_frames = 1};
        }
        return TemporalRequirements{};
    }

    [[nodiscard]] std::string_view layer_id() const noexcept { return m_layer_id; }
    void set_layer_id(std::string id) { m_layer_id = std::move(id); }

    [[nodiscard]] std::uint32_t layer_index() const noexcept { return m_layer_index; }
    [[nodiscard]] std::uint32_t item_index() const noexcept { return m_item_index; }
    void set_binding_location(std::uint32_t layer_index, std::uint32_t item_index) noexcept {
        m_layer_index = layer_index;
        m_item_index = item_index;
    }

    using OpacityEvaluator = std::function<float(const RenderFrameInfo&)>;

    void set_opacity_evaluator(OpacityEvaluator eval) { m_opacity_eval = std::move(eval); }

    [[nodiscard]] float evaluate_opacity(const RenderFrameInfo& info) const {
        return m_opacity_eval ? m_opacity_eval(info) : 1.0f;
    }

    [[nodiscard]] virtual bool can_seed_full_frame(const RenderGraphContext&) const noexcept {
        return false;
    }

    [[nodiscard]] virtual bool has_compiled_recorder() const noexcept {
        return false;
    }

    [[nodiscard]] RenderNodeCachePolicy cache_policy() const noexcept {
        return m_cache_policy;
    }

    [[nodiscard]] virtual cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const = 0;

    virtual NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) = 0;

protected:
    RenderNodeCachePolicy m_cache_policy{frame_variant_cache("default")};

private:
    std::string m_layer_id;
    OpacityEvaluator m_opacity_eval;
    std::uint32_t m_layer_index{UINT32_MAX};
    std::uint32_t m_item_index{0};
};

} // namespace chronon3d::graph
