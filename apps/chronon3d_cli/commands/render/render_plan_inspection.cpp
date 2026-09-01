#include "render_plan_inspection.hpp"

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

#include <fmt/format.h>

#include <optional>
#include <string>
#include <utility>

namespace chronon3d::cli {

namespace {

std::string layer_type_name(render_plan::LayerType type) {
    switch (type) {
        case render_plan::LayerType::Image:         return "image";
        case render_plan::LayerType::Video:         return "video";
        case render_plan::LayerType::Text:          return "text";
        case render_plan::LayerType::Color:         return "color";
    }
    return "unknown";
}

std::string output_format_name(render_plan::OutputFormat format) {
    switch (format) {
        case render_plan::OutputFormat::Png:  return "png";
        case render_plan::OutputFormat::Mp4:  return "mp4";
        case render_plan::OutputFormat::Mkv:  return "mkv";
        case render_plan::OutputFormat::WebM: return "webm";
    }
    return "unknown";
}

std::string video_codec_name(render_plan::VideoCodec codec) {
    switch (codec) {
        case render_plan::VideoCodec::Auto: return "auto";
        case render_plan::VideoCodec::H264: return "h264";
        case render_plan::VideoCodec::H265: return "h265";
        case render_plan::VideoCodec::VP9:  return "vp9";
        case render_plan::VideoCodec::AV1:  return "av1";
    }
    return "auto";
}

std::string blend_mode_name(chronon3d::BlendMode mode) {
    switch (mode) {
        case chronon3d::BlendMode::Normal:     return "normal";
        case chronon3d::BlendMode::Add:        return "add";
        case chronon3d::BlendMode::Multiply:   return "multiply";
        case chronon3d::BlendMode::Screen:     return "screen";
        case chronon3d::BlendMode::Overlay:    return "overlay";
        case chronon3d::BlendMode::Darken:     return "darken";
        case chronon3d::BlendMode::Lighten:    return "lighten";
        case chronon3d::BlendMode::Difference: return "difference";
        case chronon3d::BlendMode::Exclusion:  return "exclusion";
        case chronon3d::BlendMode::SoftLight:  return "soft_light";
        case chronon3d::BlendMode::HardLight:  return "hard_light";
        case chronon3d::BlendMode::ColorDodge: return "color_dodge";
        case chronon3d::BlendMode::ColorBurn:  return "color_burn";
    }
    return "normal";
}

/// True when the given logical font path is present in the prepared asset
/// manifest with Font kind.  This is a read-only derivation from the already
/// resolved manifest — it never re-resolves the asset.
bool font_is_resolved(const PreparedRenderPlanContext& context,
                      const std::string& logical_path) {
    if (logical_path.empty()) {
        return false;
    }
    for (const auto& asset : context.prepared.assets.assets()) {
        if (asset.kind == chronon3d::assets::PreparedAssetKind::Font &&
            asset.logical_path == logical_path) {
            return true;
        }
    }
    return false;
}

} // namespace

ResolvedRenderPlanInspection
build_render_plan_inspection(const PreparedRenderPlanContext& context) {
    ResolvedRenderPlanInspection inspection;

    const auto& decoded = context.decoded;
    const auto& prepared = context.prepared;

    inspection.job.id = prepared.job_id;
    inspection.job.schema = "chronon.render-plan.v2";
    inspection.job.content_digest = prepared.fingerprint.content_digest.hex();
    inspection.job.request_digest = prepared.fingerprint.request_digest.hex();
    inspection.job.asset_manifest_digest = prepared.assets.manifest_digest().hex();

    inspection.canvas.width = prepared.canvas.width;
    inspection.canvas.height = prepared.canvas.height;
    inspection.canvas.fps_num = prepared.canvas.fps.num();
    inspection.canvas.fps_den = prepared.canvas.fps.den();
    inspection.canvas.frames = static_cast<int>(prepared.canvas.duration.integral());

    inspection.output_path = prepared.output.path;
    inspection.output_format = output_format_name(prepared.output.format);
    inspection.video_codec = video_codec_name(prepared.output.codec);

    inspection.layers.reserve(decoded.layers.size());
    for (const auto& layer : decoded.layers) {
        ResolvedLayerInspection resolved;
        resolved.id = layer.id;
        resolved.type = layer_type_name(layer.type);
        resolved.asset = layer.asset;
        resolved.source = layer.source;
        resolved.text = layer.text;

        const std::string font_path = layer.font;
        resolved.font.asset = font_path;
        resolved.font.resolved = font_is_resolved(context, font_path);

        if (layer.style) {
            if (layer.style->font_size) {
                resolved.font_size = layer.style->font_size;
            }
            resolved.fill = layer.style->fill;
            if (layer.style->stroke) {
                resolved.stroke = layer.style->stroke->color;
                if (layer.style->stroke->width) {
                    resolved.stroke +=
                        fmt::format(" {:.2f}px", *layer.style->stroke->width);
                }
            }
            if (layer.style->background) {
                resolved.background = layer.style->background->color;
            }
        }

        if (layer.position_dimensions >= 2) {
            resolved.layout.x = layer.position[0];
            resolved.layout.y = layer.position[1];
        }
        if (layer.size_dimensions == 2) {
            resolved.layout.width = layer.size[0];
            resolved.layout.height = layer.size[1];
        }

        if (layer.animation && !layer.animation->tracks.empty()) {
            resolved.motion.preset = layer.animation->tracks[0].property;
        }

        if (layer.blend_mode) {
            resolved.blend_mode = blend_mode_name(*layer.blend_mode);
        }
        resolved.opacity = layer.opacity;
        resolved.loop = layer.loop;
        if (layer.start_frame) {
            resolved.start_frame = static_cast<int>(layer.start_frame->integral());
        }
        if (layer.duration_frames) {
            resolved.duration_frames =
                static_cast<int>(layer.duration_frames->integral());
        }

        inspection.layers.push_back(std::move(resolved));
    }

    return inspection;
}

} // namespace chronon3d::cli
