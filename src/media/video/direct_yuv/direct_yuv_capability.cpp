#include <chronon3d/media/video/direct_yuv_program.hpp>
#include "direct_yuv_text_contract.hpp"

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <cmath>
#include <exception>
#include <string>
#include <string_view>

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
        // The text contract classifier owns the text-specific reason so the
        // reported class stays precise; the declared matrix owns the lane
        // decision it feeds.
        std::string_view text_reason;

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
                    // One classifier: the canonical prepared layout/MTSDF
                    // contract, the representability of every remaining style
                    // semantic (including the single canonical drop shadow the
                    // CUDA kernel resamples from the same distance field), and
                    // the exact rejection reason. Both flags it produces are
                    // inputs of the declared matrix below.
                    const auto text_contract =
                        resolve_direct_yuv_text_contract(*handle.value);
                    query.prepared_mtsdf = text_contract.prepared_mtsdf;
                    query.text_style_representable =
                        text_contract.style_representable;
                    if (text_contract.rejection) {
                        text_reason = text_contract.rejection;
                    }
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
            const std::string_view reason =
                text_reason.empty() ? eligibility.reason : text_reason;
            report.reason = std::string(reason) +
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
