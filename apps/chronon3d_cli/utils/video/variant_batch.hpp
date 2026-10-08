#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
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
    /// Optional semantic identities for whole-output DAG sharing. Empty keys
    /// are treated as variant-local, never as evidence that content is equal.
    std::string base_visual_fingerprint{};
    std::string layout_fingerprint{};
    std::string audio_fingerprint{};
    /// Optional per-output audio source. Empty keeps the job's shared audio.
    std::string audio_source{};
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

/// A per-frame multi-output DAG. `base_visual_fingerprint` identifies shared
/// video work before locale/layout composition; the layout and audio keys
/// identify their independently shareable stages.
struct VariantDagPlan {
    struct Output {
        std::size_t variant_index{0};
        std::size_t base_source_index{0};
        std::size_t layout_source_index{0};
        std::size_t audio_source_index{0};
        std::string base_key;
        std::string layout_key;
        std::string audio_key;
    };
    std::vector<Output> outputs;
};

[[nodiscard]] inline VariantDagPlan plan_variant_dag(
    std::span<const OutputVariant> variants) {
    VariantDagPlan plan;
    plan.outputs.reserve(variants.size());
    std::map<std::string, std::size_t> bases, layouts, audios;
    for (std::size_t i = 0; i < variants.size(); ++i) {
        const auto& variant = variants[i];
        const auto unique_key = [i](const std::string& key, const char* stage) {
            return key.empty() ? std::string(stage) + ":variant:" + std::to_string(i) : key;
        };
        const auto base_key = unique_key(variant.base_visual_fingerprint, "base");
        const auto layout_key = base_key + "\n" + unique_key(variant.layout_fingerprint, "layout");
        const auto audio_key = unique_key(variant.audio_fingerprint, "audio");
        const auto [base_it, base_inserted] = bases.emplace(base_key, i);
        const auto [layout_it, layout_inserted] = layouts.emplace(layout_key, i);
        const auto [audio_it, audio_inserted] = audios.emplace(audio_key, i);
        (void)base_inserted; (void)layout_inserted; (void)audio_inserted;
        plan.outputs.push_back({i, base_it->second, layout_it->second, audio_it->second,
                                base_key, layout_key, audio_key});
    }
    return plan;
}

/// Execute one frame through the shared stage DAG. Callers keep the returned
/// GPU handles/surfaces alive until every output has been submitted.
template <typename VideoValue, typename AudioValue, typename RenderBase,
          typename ComposeLayout, typename PrepareAudio, typename Submit>
[[nodiscard]] bool execute_variant_dag(
    const VariantDagPlan& plan,
    RenderBase&& render_base,
    ComposeLayout&& compose_layout,
    PrepareAudio&& prepare_audio,
    Submit&& submit) {
    std::map<std::string, VideoValue> bases, layouts;
    std::map<std::string, AudioValue> audios;
    for (const auto& output : plan.outputs) {
        auto base = bases.find(output.base_key);
        if (base == bases.end())
            base = bases.emplace(output.base_key,
                render_base(output.base_source_index)).first;
        auto layout = layouts.find(output.layout_key);
        if (layout == layouts.end())
            layout = layouts.emplace(output.layout_key,
                compose_layout(base->second, output.layout_source_index)).first;
        auto audio = audios.find(output.audio_key);
        if (audio == audios.end())
            audio = audios.emplace(output.audio_key,
                prepare_audio(output.audio_source_index)).first;
        if (!submit(output.variant_index, layout->second, audio->second)) return false;
    }
    return true;
}

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
