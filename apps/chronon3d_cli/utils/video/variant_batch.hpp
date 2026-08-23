#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct OutputVariant {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::string pixel_format{};
    std::string codec{};
    std::string output{};
};

enum class VariantReuseMode : std::uint8_t {
    MasterScale,
    SharedResourcesRecompose,
    Independent,
};

struct VariantBatchPlan {
    std::size_t master_index{0};
    VariantReuseMode mode{VariantReuseMode::Independent};
    bool share_decode{false};
    bool share_shaping{false};
    bool share_compilation{false};
    struct Execution {
        std::size_t variant_index{0};
        std::size_t source_variant_index{0};
        bool scale_master{false};
        bool recompose_layout{false};
    };
    std::vector<Execution> executions{};
};

/// Run one frame through a planned variant batch. `render_master` is invoked
/// exactly once; all other work is explicit in the scale/recompose callbacks.
/// The returned frame values are intentionally opaque so this contract can be
/// used by CPU and GPU exporters without creating a second renderer or cache.
template <typename FrameValue, typename RenderMaster, typename ScaleMaster,
          typename RecomposeLayout, typename Submit>
[[nodiscard]] bool execute_variant_batch(
    const VariantBatchPlan& plan,
    RenderMaster&& render_master,
    ScaleMaster&& scale_master,
    RecomposeLayout&& recompose_layout,
    Submit&& submit) {
    if (plan.executions.empty()) return false;
    const FrameValue master = render_master(plan.master_index);
    for (const auto& execution : plan.executions) {
        FrameValue output = master;
        if (execution.scale_master) {
            output = scale_master(master, execution.variant_index);
        } else if (execution.recompose_layout) {
            output = recompose_layout(master, execution.variant_index);
        }
        if (!submit(execution.variant_index, output)) return false;
    }
    return true;
}

[[nodiscard]] constexpr bool same_aspect_ratio(
    const OutputVariant& a, const OutputVariant& b) noexcept {
    return a.width != 0 && a.height != 0 && b.width != 0 && b.height != 0 &&
        static_cast<std::uint64_t>(a.width) * b.height ==
        static_cast<std::uint64_t>(b.width) * a.height;
}

[[nodiscard]] inline VariantBatchPlan plan_variant_batch(
    std::span<const OutputVariant> variants) {
    if (variants.empty()) return {};
    const auto& master = variants.front();
    bool same_aspect = true;
    for (const auto& variant : variants.subspan(1)) {
        if (!same_aspect_ratio(master, variant)) {
            same_aspect = false;
            break;
        }
    }
    if (same_aspect) {
        VariantBatchPlan plan{
            .master_index = 0,
            .mode = VariantReuseMode::MasterScale,
            .share_decode = true,
            .share_shaping = true,
            .share_compilation = true};
        plan.executions.reserve(variants.size());
        for (std::size_t i = 0; i < variants.size(); ++i) {
            plan.executions.push_back({i, 0, i != 0, false});
        }
        return plan;
    }
    VariantBatchPlan plan{
        .master_index = 0,
        .mode = VariantReuseMode::SharedResourcesRecompose,
        .share_decode = true,
        .share_shaping = true,
        .share_compilation = true};
    plan.executions.reserve(variants.size());
    for (std::size_t i = 0; i < variants.size(); ++i) {
        plan.executions.push_back({i, 0, false, i != 0});
    }
    return plan;
}

} // namespace chronon3d::cli
