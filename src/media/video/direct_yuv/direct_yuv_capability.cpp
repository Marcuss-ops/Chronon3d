#include <chronon3d/media/video/direct_yuv_program.hpp>

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <cmath>
#include <exception>

namespace chronon3d::media::video {
namespace {

bool is_2d_transform_for_capability(const Transform& transform) noexcept {
    return std::abs(transform.rotation.x) < 1e-3f &&
           std::abs(transform.rotation.y) < 1e-3f &&
           std::abs(transform.position.z) < 1e-3f &&
           std::abs(transform.anchor.z) < 1e-3f;
}

} // namespace

DirectYuvProgram::CapabilityReport DirectYuvProgram::resolve_support(
    const CompiledComposition& compiled, DirectYuvPolicy policy) {
    CapabilityReport report;
    if (!compiled.composition) {
        report.reason = "compiled composition is empty";
        return report;
    }

    Scene scene;
    try {
        const auto composition = compiled.composition;
        scene = composition->evaluate(make_frame_context({
            .global_time = SampleTime::from_frame(0.0, composition->frame_rate()),
            .duration = composition->duration(),
            .width = composition->width(),
            .height = composition->height(),
        }));
    } catch (const std::exception& error) {
        report.reason = std::string("scene capability evaluation failed: ") + error.what();
        return report;
    }

    bool video_seen = false;
    for (const auto& layer : scene.layers()) {
        if (layer.kind == LayerKind::Null || layer.kind == LayerKind::Audio) continue;

        LayerYuvQuery query;
        query.two_d_transform = is_2d_transform_for_capability(layer.transform);
        query.mask_enabled = layer.mask.enabled();
        query.projection_enabled = layer.uses_2_5d_projection;
        query.blend = layer.blend_mode;

        if (layer.video_source) {
            query.effect = video_seen
                ? DirectYuvEffectClass::VideoOverlayNv12
                : DirectYuvEffectClass::VideoBackground;
            video_seen = true;
        } else if (!layer.nodes.empty()) {
            const auto shape = layer.nodes.front().shape.type();
            if (shape == ShapeType::Image) {
                query.effect = DirectYuvEffectClass::ImageOverlay;
                query.gpu_resident = true;
            } else if (shape == ShapeType::TextRun || layer.kind == LayerKind::Text) {
                query.effect = DirectYuvEffectClass::TextOverlay;
                query.gpu_resident = true;
            } else {
                query.effect = DirectYuvEffectClass::BasicShapeOverlay;
            }
        } else {
            continue;
        }

        const auto eligibility = resolve_direct_yuv_support(query, policy);
        if (!eligibility.eligible) {
            report.support = eligibility.support;
            report.reason = std::string(eligibility.reason) +
                " (layer '" + std::string(layer.name) + "')";
            return report;
        }
    }

    if (!video_seen) {
        report.reason = "no video background for DirectYUV";
        report.support = DirectYuvSupport::Unsupported;
        return report;
    }

    report.eligible = true;
    report.support = DirectYuvSupport::Certified;
    report.reason = "certified";
    return report;
}

} // namespace chronon3d::media::video
