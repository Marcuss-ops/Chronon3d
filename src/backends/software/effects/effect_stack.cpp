// effect_stack.cpp — registry-backed software effect dispatch.

#include "render_effects_processor.hpp"
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include <stdexcept>
#include <vector>

namespace chronon3d::renderer {

namespace {

// Build one real ColorPipeline stage from the canonical effect parameters.
// Returning false is deliberate: malformed or unsupported entries must go
// through the normal processor path and retain its validation/error contract.
bool append_fusible_stage(
    const effects::EffectInstance& effect,
    const effects::EffectExecutionContext& context,
    ColorPipeline& pipeline) {
    return std::visit([&](const auto& params) -> bool {
        using Params = std::remove_cvref_t<decltype(params)>;
        if constexpr (std::is_same_v<Params, ExposureParams>) {
            if (effect.effect_type != effects::EffectType::Exposure) return false;
            pipeline.add_stage(ExposureStage{params.stops});
            return true;
        } else if constexpr (std::is_same_v<Params, LevelsParams>) {
            if (effect.effect_type != effects::EffectType::Levels) return false;
            pipeline.add_stage(LevelsStage{
                .master = {params.master.input_black, params.master.input_white,
                           params.master.gamma, params.master.output_black,
                           params.master.output_white},
                .red = {params.red.input_black, params.red.input_white,
                        params.red.gamma, params.red.output_black,
                        params.red.output_white},
                .green = {params.green.input_black, params.green.input_white,
                          params.green.gamma, params.green.output_black,
                          params.green.output_white},
                .blue = {params.blue.input_black, params.blue.input_white,
                         params.blue.gamma, params.blue.output_black,
                         params.blue.output_white}});
            return true;
        } else if constexpr (std::is_same_v<Params, CurvesParams>) {
            if (effect.effect_type != effects::EffectType::Curves || !context.curve_cache)
                return false;
            CurvesStage stage;
            if (!params.master.empty())
                stage.master = context.curve_cache->get_or_compile(params.master);
            if (!params.red.empty())
                stage.red = context.curve_cache->get_or_compile(params.red);
            if (!params.green.empty())
                stage.green = context.curve_cache->get_or_compile(params.green);
            if (!params.blue.empty())
                stage.blue = context.curve_cache->get_or_compile(params.blue);
            pipeline.add_stage(std::move(stage));
            return true;
        } else {
            return false;
        }
    }, effect.params);
}

} // namespace

void apply_effect_stack(
    Framebuffer& fb,
    const EffectStack& stack,
    const effects::EffectExecutionContext& context,
    std::span<const EffectProcessorHandle> processors,
    std::shared_ptr<const ProcessorRegistrySnapshot> snapshot) {
    if (processors.size() != stack.size()) {
        throw std::runtime_error(
            "Compiled effect processor binding count does not match effect stack");
    }
    ColorPipeline fused_pipeline;
    bool has_fused_stages = false;
    const auto flush_fused_pipeline = [&]() {
        if (!has_fused_stages) return;
        if (context.counters) {
            context.counters->color_pipeline_batches.fetch_add(
                1, std::memory_order_relaxed);
            context.counters->color_pipeline_effects.fetch_add(
                static_cast<uint64_t>(fused_pipeline.stage_count()),
                std::memory_order_relaxed);
        }
        fused_pipeline.apply(fb, context.clip);
        fused_pipeline.clear();
        has_fused_stages = false;
    };

    for (std::size_t index = 0; index < stack.size(); ++index) {
        const auto& effect = stack[index];
        if (!effect.enabled) continue;

        if (append_fusible_stage(effect, context, fused_pipeline)) {
            has_fused_stages = true;
            continue;
        }

        // A non-fusible effect is a semantic barrier. Flush the preceding
        // color stages before dispatching it through its canonical processor.
        flush_fused_pipeline();

        const auto processor = snapshot
            ? snapshot->effect_shared(processors[index])
            : nullptr;
        if (!processor) {
            throw std::runtime_error(
                "Missing compiled software effect processor for effect type " +
                std::to_string(static_cast<int>(effect.effect_type)));
        }
        processor->apply(fb, effect.params, context);
    }
    flush_fused_pipeline();
}

void apply_effect_stack(
    Framebuffer& fb,
    const EffectStack& stack,
    const effects::EffectExecutionContext& context,
    SoftwareRegistry& registry) {
    const auto snapshot = registry.snapshot();
    std::vector<EffectProcessorHandle> handles;
    handles.reserve(stack.size());
    for (const auto& effect : stack) {
        handles.push_back(effect.enabled
            ? snapshot->effect_handle(effect.param_type_index())
            : EffectProcessorHandle{});
    }
    apply_effect_stack(fb, stack, context, handles, snapshot);
}

} // namespace chronon3d::renderer
