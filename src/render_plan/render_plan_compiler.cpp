#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/presets/text/subtitle.hpp>
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <fstream>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chronon3d::render_plan {
namespace {

std::string read_asset_text(const std::string& path,
                           const std::string& assets_root) {
    std::filesystem::path resolved_path{path};
    if (!assets_root.empty() && resolved_path.is_relative()) {
        resolved_path = std::filesystem::path{assets_root} / resolved_path;
    }
    std::ifstream input(resolved_path);
    if (!input) throw std::runtime_error("cannot read subtitle asset: " + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

chronon3d::FitMode fit_mode(FitMode value) {
    switch (value) {
        case FitMode::Contain: return chronon3d::FitMode::Contain;
        case FitMode::Stretch: return chronon3d::FitMode::Stretch;
        case FitMode::None: return chronon3d::FitMode::None;
        case FitMode::Cover: return chronon3d::FitMode::Cover;
    }
    return chronon3d::FitMode::Cover;
}

chronon3d::TextDefinition text_definition(const LayerPlan& layer,
                                           const chronon3d::CanvasInfo& canvas) {
    if (layer.preset == "title_centered")
        return chronon3d::presets::text::title_centered(layer.text, canvas);
    if (layer.preset == "subtitle_bottom")
        return chronon3d::presets::text::subtitle_bottom(layer.text, canvas);
    if (layer.preset == "caption_safe_area")
        return chronon3d::presets::text::caption_safe_area(layer.text, canvas);
    if (layer.preset == "kinetic_word")
        return chronon3d::presets::text::kinetic_word(layer.text, canvas);
    if (layer.preset == "lower_third")
        return chronon3d::presets::text::lower_third(layer.text, canvas);

    chronon3d::TextDefinition definition;
    definition.content.value = layer.text;
    definition.style.font.font_path = layer.font;
    definition.style.font.font_size = layer.font_size.value_or(48.0f);
    definition.style.color = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
    definition.frame.size = {
        layer.box_width.value_or(static_cast<float>(canvas.width)),
        layer.box_height.value_or(static_cast<float>(canvas.height))};
    definition.frame.align = chronon3d::TextAlign::Center;
    definition.frame.vertical_align = chronon3d::VerticalAlign::Middle;
    return definition;
}

void apply_layer_timing(chronon3d::LayerBuilder& builder, const LayerPlan& layer) {
    if (layer.start_frame) builder.from(*layer.start_frame);
    if (layer.duration_frames) builder.duration(*layer.duration_frames);
    if (layer.position_dimensions >= 2)
        builder.position({layer.position[0], layer.position[1], layer.position[2]});
    if (layer.animation) {
        if (layer.animation->start_frame) builder.from(*layer.animation->start_frame);
        if (layer.animation->duration_frames)
            builder.duration(*layer.animation->duration_frames);
        if (!layer.animation->preset.empty()) builder.motion(layer.animation->preset);
    }
}

}  // namespace

Result<std::shared_ptr<const Composition>, PlanDecodeError>
compile_render_plan(const RenderPlan& plan) {
    try {
        const auto canvas = CanvasInfo::from_dimensions(
            static_cast<float>(plan.canvas.width),
            static_cast<float>(plan.canvas.height));
        std::vector<std::optional<presets::text::SubtitleTrack>> subtitles(
            plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::SubtitleTrack) continue;
            const auto raw = read_asset_text(layer.source, plan.assets_root);
            if (layer.subtitle_format == SubtitleFormat::Vtt)
                subtitles[index] = presets::text::subtitle_from_vtt(raw);
            else if (layer.subtitle_format == SubtitleFormat::Json)
                subtitles[index] = presets::text::subtitle_from_json(raw);
            else
                subtitles[index] = presets::text::subtitle_from_srt(raw);
        }

        CompositionSpec spec;
        spec.name = plan.job_id;
        spec.width = plan.canvas.width;
        spec.height = plan.canvas.height;
        spec.frame_rate = {plan.canvas.fps, 1};
        spec.duration = plan.canvas.duration;

        auto composition = std::make_shared<Composition>(
            std::move(spec), [plan, canvas, subtitles](const FrameContext& ctx) {
                SceneBuilder scene(ctx);
                for (std::size_t index = 0; index < plan.layers.size(); ++index) {
                    const auto& layer = plan.layers[index];
                    scene.layer(layer.id, [&](LayerBuilder& builder) {
                        switch (layer.type) {
                            case LayerType::Image: {
                                ImageParams params;
                                params.asset_path = layer.asset;
                                params.size = {
                                    layer.box_width.value_or(static_cast<float>(plan.canvas.width)),
                                    layer.box_height.value_or(static_cast<float>(plan.canvas.height))};
                                params.fit = fit_mode(layer.fit.value_or(FitMode::Cover));
                                builder.image("image", std::move(params));
                                break;
                            }
                            case LayerType::Video:
                                builder.video(layer.source);
                                break;
                            case LayerType::Color: {
                                RectParams params;
                                params.size = {static_cast<float>(plan.canvas.width),
                                               static_cast<float>(plan.canvas.height)};
                                params.color = {layer.color[0], layer.color[1],
                                                layer.color[2], layer.color[3]};
                                builder.rect("color", std::move(params));
                                break;
                            }
                            case LayerType::Text:
                                builder.kind(LayerKind::Text);
                                builder.text("text", text_definition(layer, canvas));
                                break;
                            case LayerType::SubtitleTrack: {
                                if (!subtitles[index])
                                    throw std::runtime_error("subtitle asset was not prepared");
                                authoring::Layer authoring_layer(builder, canvas);
                                auto track = authoring_layer.subtitles(*subtitles[index]);
                                track.preset(layer.preset.empty() ? "minimal_white" : layer.preset)
                                    .font(layer.font, layer.font_size.value_or(48.0f))
                                    .build();
                                break;
                            }
                        }
                        apply_layer_timing(builder, layer);
                    });
                }
                return scene.build();
            });
        return std::shared_ptr<const Composition>(std::move(composition));
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
