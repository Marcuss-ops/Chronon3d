#include <chronon3d/specscene/model/specscene.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/depth_role.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/transform.hpp>
#include "../parser/specscene_parsers.hpp"
#include <fmt/format.h>
#include <filesystem>

namespace chronon3d::specscene {

bool is_specscene_file(const std::filesystem::path& path) {
    auto ext = lower_copy(path.extension().string());
    return ext == ".specscene" || ext == ".toml";
}

std::optional<SpecSceneDocument> load_file(const std::filesystem::path& path,
                                           std::vector<std::string>* diagnostics) {
    try {
        const auto parsed = toml::parse_file(path.string());
        return parse_document(parsed, diagnostics);
    } catch (const std::exception& e) {
        note(diagnostics, fmt::format("failed to parse `{}`: {}", path.string(), e.what()));
        return std::nullopt;
    }
}

std::optional<Composition> compile_file(const std::filesystem::path& path,
                                        std::vector<std::string>* diagnostics) {
    auto doc = load_file(path, diagnostics);
    if (!doc) return std::nullopt;

    CompositionSpec spec;
    spec.name = doc->scene.name;
    spec.width = doc->scene.width;
    spec.height = doc->scene.height;
    spec.frame_rate = doc->scene.frame_rate;
    spec.duration = doc->scene.duration;

    SceneDescription scene = std::move(doc->scene);
    Composition comp(spec, [scene = std::move(scene)](const FrameContext& ctx) {
        TimelineEvaluator evaluator;
        return evaluator.evaluate(scene, ctx.frame, ctx.resource);
    });

    if (doc->has_render_camera) {
        comp.camera = doc->render_camera;
    }

    return comp;
}

} // namespace chronon3d::specscene
