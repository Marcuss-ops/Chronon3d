#include <chronon3d/media/video/direct_yuv_program.hpp>

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/text_run_shape.hpp>

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

// DirectYUV lowers a TextRun from the canonical prepared layout into the
// shared MTSDF glyph representation. Keep this gate next to capability
// resolution: accepting richer text here would silently discard glyph state
// in the CUDA lowering.
[[nodiscard]] const char* direct_yuv_text_rejection(
    const TextRunShape& shape) noexcept {
    if (!shape.layout) return "text_missing_canonical_layout";
    if (shape.layout->placed.glyphs.empty()) return "text_layout_has_no_glyphs";
    if (shape.animated_doc) return "text_animated_document_unsupported";
    if (shape.dissolve_layout || !shape.dissolve_glyphs.empty()) {
        return "text_dissolve_unsupported";
    }
    for (const auto& animator : shape.animators) {
        if (animator.enabled) return "text_glyph_animation_unsupported";
    }
    if (shape.layout->font_spans.empty()) return "text_missing_font_spans";
    for (const auto& shadow : shape.shadows) {
        if (shadow.enabled) return "text_shadow_unsupported";
    }
    if (shape.paint.fill_style || shape.paint.stroke_style) {
        return "text_gradient_paint_unsupported";
    }
    if (shape.material.enabled) return "text_material_unsupported";
    return nullptr;
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
            if (layer.nodes.size() > 1) {
                report.support = DirectYuvSupport::Unsupported;
                report.reason = "multi_node_layer_unsupported (layer '" +
                    std::string(layer.name) + "')";
                return report;
            }
            const auto shape = layer.nodes.front().shape.type();
            if (shape == ShapeType::Image) {
                query.effect = DirectYuvEffectClass::ImageOverlay;
                query.gpu_resident = true;
            } else if (shape == ShapeType::TextRun || layer.kind == LayerKind::Text) {
                query.effect = DirectYuvEffectClass::TextOverlay;
                query.gpu_resident = true;
                if (shape == ShapeType::TextRun) {
                    const auto& handle = layer.nodes.front().shape.text_run_shape_handle();
                    if (!handle.value) {
                        report.support = DirectYuvSupport::Unsupported;
                        report.reason = "text_missing_prepared_shape (layer '" +
                            std::string(layer.name) + "')";
                        return report;
                    }
                    if (const char* rejection = direct_yuv_text_rejection(*handle.value)) {
                        report.support = DirectYuvSupport::Unsupported;
                        report.reason = std::string(rejection) + " (layer '" +
                            std::string(layer.name) + "')";
                        return report;
                    }
                    // These checks establish the canonical
                    // PreparedTextRun/MTSDF contract consumed by the CUDA
                    // lowering. Do not admit arbitrary GPU text textures.
                    query.prepared_mtsdf = true;
                } else {
                    report.support = DirectYuvSupport::Unsupported;
                    report.reason = "text_not_using_prepared_text_run (layer '" +
                        std::string(layer.name) + "')";
                    return report;
                }
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
