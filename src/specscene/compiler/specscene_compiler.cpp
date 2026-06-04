#include <chronon3d/specscene/compiler/specscene_compiler.hpp>
#include <chronon3d/specscene/model/specscene.hpp>
#include <chronon3d/specscene/validation/specscene_validator.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/description/scene_description.hpp>
#include <fmt/format.h>
#include <filesystem>

namespace chronon3d::specscene {

std::optional<Composition> compile_document(
    const SpecSceneDocument& doc,
    std::vector<std::string>* diagnostics)
{
    // Validate before compiling
    if (diagnostics) {
        if (!validate_document(doc, *diagnostics)) {
            return std::nullopt;
        }
    }

    CompositionSpec spec;
    spec.name = doc.scene.name;
    spec.width = doc.scene.width;
    spec.height = doc.scene.height;
    spec.frame_rate = doc.scene.frame_rate;
    spec.duration = doc.scene.duration;

    SceneDescription scene = doc.scene;  // copy
    Composition comp(spec, [scene = std::move(scene)](const FrameContext& ctx) {
        TimelineEvaluator evaluator;
        return evaluator.evaluate(scene, ctx.frame, ctx.resource);
    });

    if (doc.has_render_camera) {
        comp.camera = doc.render_camera;
    }

    return comp;
}

std::optional<Composition> compile_file(
    const std::filesystem::path& path,
    std::vector<std::string>* diagnostics)
{
    auto doc = load_file(path, diagnostics);
    if (!doc) return std::nullopt;

    return compile_document(*doc, diagnostics);
}

} // namespace chronon3d::specscene
