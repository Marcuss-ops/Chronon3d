#include "direct_yuv_program.hpp"

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/cuda_image_resource.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/text/text_definition.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

#ifdef CHRONON3D_USE_BLEND2D
#include <blend2d.h>
#endif

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <filesystem>
#include <utility>

namespace chronon3d::cli {
namespace {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

bool is_2d_transform(const Transform& t) {
    return std::abs(t.rotation.x) < 1e-3f &&
           std::abs(t.rotation.y) < 1e-3f &&
           std::abs(t.position.z) < 1e-3f &&
           std::abs(t.anchor.z) < 1e-3f;
}

std::string resolve_font_file(const std::string& requested) {
    if (requested.empty()) {
        throw std::runtime_error("text layer has no prepared font asset");
    }
    if (!std::filesystem::exists(requested)) {
        throw std::runtime_error("prepared font asset not found: " + requested);
    }
    return requested;
}

struct DirectRasterizedText {
    int width{0};
    int height{0};
    std::vector<float> gpu_rgba;
    assets::ContentDigest digest{};
};

#ifdef CHRONON3D_USE_BLEND2D
DirectRasterizedText rasterize_text_direct(
    const std::string& text,
    const std::string& font_path_req,
    float font_size,
    const Color& fill_color,
    const Color& stroke_color,
    float stroke_width,
    bool has_background,
    const Color& bg_color,
    float bg_opacity,
    float bg_radius,
    float pad_x,
    float pad_y,
    float box_w,
    float box_h,
    const std::string& alignment) {

    const std::string font_file = resolve_font_file(font_path_req);
    BLFontFace face;
    BLResult face_res = face.createFromFile(font_file.c_str());
    if (face_res != BL_SUCCESS) {
        throw std::runtime_error("prepared font asset could not be loaded: " + font_file);
    }

    const float effective_font_size = font_size > 0.0f ? font_size : 58.0f;
    BLFont font;
    font.createFromFace(face, effective_font_size);
    BLFontMetrics fm = font.metrics();

    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (lines.empty()) lines.push_back(text.empty() ? " " : text);

    std::vector<float> line_widths;
    float max_line_w = 0.0f;
    for (const auto& line : lines) {
        BLGlyphBuffer gb;
        gb.setUtf8Text(line.data(), line.size());
        font.shape(gb);
        BLTextMetrics tm{};
        font.getTextMetrics(gb, tm);
        float w = static_cast<float>(tm.advance.x);
        if (w <= 0.0f) w = static_cast<float>(line.size()) * effective_font_size * 0.55f;
        line_widths.push_back(w);
        if (w > max_line_w) max_line_w = w;
    }

    const float line_spacing = fm.ascent + fm.descent + fm.lineGap;
    const float total_text_h = lines.size() > 1
        ? (fm.ascent + fm.descent + static_cast<float>(lines.size() - 1) * line_spacing)
        : (fm.ascent + fm.descent);

    float target_w = max_line_w;
    float target_h = total_text_h;

    if (has_background) {
        target_w += pad_x * 2.0f;
        target_h += pad_y * 2.0f;
        if (box_w > target_w) target_w = box_w;
        if (box_h > target_h) target_h = box_h;
    } else {
        target_w += stroke_width * 2.0f + 24.0f;
        target_h += stroke_width * 2.0f + 16.0f;
    }

    int img_w = (static_cast<int>(std::ceil(target_w)) + 3) & ~3;
    int img_h = (static_cast<int>(std::ceil(target_h)) + 1) & ~1;
    if (img_w < 16) img_w = 16;
    if (img_h < 16) img_h = 16;

    BLImage bl_img(img_w, img_h, BL_FORMAT_PRGB32);
    BLContext ctx(bl_img);
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
    ctx.fillAll();

    ctx.setCompOp(BL_COMP_OP_SRC_OVER);

    if (has_background) {
        const float card_w = max_line_w + pad_x * 2.0f;
        const float card_h = total_text_h + pad_y * 2.0f;
        const float card_x = (static_cast<float>(img_w) - card_w) * 0.5f;
        const float card_y = (static_cast<float>(img_h) - card_h) * 0.5f;
        const uint32_t bg_a_byte = static_cast<uint32_t>(std::clamp(bg_opacity * bg_color.a, 0.0f, 1.0f) * 255.0f);
        const uint32_t bg_r_byte = static_cast<uint32_t>(std::clamp(bg_color.r, 0.0f, 1.0f) * 255.0f);
        const uint32_t bg_g_byte = static_cast<uint32_t>(std::clamp(bg_color.g, 0.0f, 1.0f) * 255.0f);
        const uint32_t bg_b_byte = static_cast<uint32_t>(std::clamp(bg_color.b, 0.0f, 1.0f) * 255.0f);
        ctx.setFillStyle(BLRgba32(bg_r_byte, bg_g_byte, bg_b_byte, bg_a_byte));
        ctx.fillRoundRect(BLRoundRect(card_x, card_y, card_w, card_h, bg_radius, bg_radius));
    }

    const uint32_t fill_a = static_cast<uint32_t>(std::clamp(fill_color.a, 0.0f, 1.0f) * 255.0f);
    const uint32_t fill_r = static_cast<uint32_t>(std::clamp(fill_color.r, 0.0f, 1.0f) * 255.0f);
    const uint32_t fill_g = static_cast<uint32_t>(std::clamp(fill_color.g, 0.0f, 1.0f) * 255.0f);
    const uint32_t fill_b = static_cast<uint32_t>(std::clamp(fill_color.b, 0.0f, 1.0f) * 255.0f);
    const BLRgba32 bl_fill(fill_r, fill_g, fill_b, fill_a);

    const uint32_t str_a = static_cast<uint32_t>(std::clamp(stroke_color.a, 0.0f, 1.0f) * 255.0f);
    const uint32_t str_r = static_cast<uint32_t>(std::clamp(stroke_color.r, 0.0f, 1.0f) * 255.0f);
    const uint32_t str_g = static_cast<uint32_t>(std::clamp(stroke_color.g, 0.0f, 1.0f) * 255.0f);
    const uint32_t str_b = static_cast<uint32_t>(std::clamp(stroke_color.b, 0.0f, 1.0f) * 255.0f);
    const BLRgba32 bl_stroke(str_r, str_g, str_b, str_a);

    float base_y = (static_cast<float>(img_h) - total_text_h) * 0.5f + fm.ascent;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        float line_w = line_widths[i];
        float tx = (static_cast<float>(img_w) - line_w) * 0.5f;
        if (alignment == "left") {
            tx = has_background
                ? ((static_cast<float>(img_w) - (max_line_w + pad_x * 2.0f)) * 0.5f + pad_x)
                : 12.0f;
        } else if (alignment == "right") {
            tx = has_background
                ? ((static_cast<float>(img_w) + (max_line_w + pad_x * 2.0f)) * 0.5f - pad_x - line_w)
                : (static_cast<float>(img_w) - 12.0f - line_w);
        }
        float ty = base_y + static_cast<float>(i) * line_spacing;

        if (stroke_width > 0.0f && stroke_color.a > 0.0f) {
            ctx.setStrokeStyle(bl_stroke);
            ctx.setStrokeWidth(stroke_width);
            ctx.strokeUtf8Text(BLPoint(tx, ty), font, line.data(), line.size());
        }
        ctx.setFillStyle(bl_fill);
        ctx.fillUtf8Text(BLPoint(tx, ty), font, line.data(), line.size());
    }
    ctx.end();

    BLImageData bl_data;
    bl_img.getData(&bl_data);
    std::vector<float> gpu_rgba(static_cast<size_t>(img_w) * img_h * 4);
    const uint8_t* src = static_cast<const uint8_t*>(bl_data.pixelData);
    size_t out_gpu = 0;
    for (int y = 0; y < img_h; ++y) {
        const uint8_t* row = src + y * bl_data.stride;
        for (int x = 0; x < img_w; ++x) {
            const float b_byte = row[x * 4 + 0];
            const float g_byte = row[x * 4 + 1];
            const float r_byte = row[x * 4 + 2];
            const float a_byte = row[x * 4 + 3];
            const float a = a_byte / 255.0f;
            if (a <= 1e-5f) {
                gpu_rgba[out_gpu++] = 0.0f;
                gpu_rgba[out_gpu++] = 0.0f;
                gpu_rgba[out_gpu++] = 0.0f;
                gpu_rgba[out_gpu++] = 0.0f;
            } else {
                const float r = (r_byte / 255.0f) / a;
                const float g = (g_byte / 255.0f) / a;
                const float b = (b_byte / 255.0f) / a;
                const auto color = Color{r, g, b, a}.to_linear().premultiplied();
                gpu_rgba[out_gpu++] = color.r;
                gpu_rgba[out_gpu++] = color.g;
                gpu_rgba[out_gpu++] = color.b;
                gpu_rgba[out_gpu++] = color.a;
            }
        }
    }

    const std::string_view bytes(
        reinterpret_cast<const char*>(gpu_rgba.data()),
        gpu_rgba.size() * sizeof(float));
    assets::ContentDigest digest = assets::sha256_string(bytes);

    DirectRasterizedText res;
    res.width = img_w;
    res.height = img_h;
    res.gpu_rgba = std::move(gpu_rgba);
    res.digest = digest;
    return res;
}
#endif

#endif

} // namespace

std::shared_ptr<DirectYuvProgram> DirectYuvProgram::prepare(
    const CompiledComposition& compiled,
    ImageCache& image_cache,
    std::shared_ptr<media::VideoDeviceRuntime> video_runtime,
    std::string& reason) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)compiled; (void)image_cache; (void)video_runtime;
    reason = "CUDA interop is not compiled";
    return nullptr;
#else
    if (!compiled.composition) {
        reason = "compiled composition is empty";
        return nullptr;
    }
    if (!video_runtime) {
        reason = "video device runtime is unavailable";
        return nullptr;
    }

    const auto duration = compiled.composition->duration();
    const auto rate = compiled.composition->frame_rate();
    const int width = compiled.composition->width();
    const int height = compiled.composition->height();

    const auto scene_t0 = profiling::now();
    const auto context_0 = make_frame_context({
        .global_time = SampleTime::from_frame(0.0, rate),
        .duration = duration,
        .width = width,
        .height = height,
    });
    Scene scene_0;
    try {
        scene_0 = compiled.composition->evaluate(context_0);
    } catch (const std::exception& error) {
        reason = std::string("scene evaluation failed: ") + error.what();
        return nullptr;
    }
    const double scene_eval_ms = profiling::duration_ms(scene_t0, profiling::now());

    std::string video_path;
    for (const auto& layer : scene_0.layers()) {
        if (layer.uses_2_5d_projection || layer.mask.enabled()) {
            reason = "layer '" + std::string(layer.name) + "' uses unsupported 3D (" +
                     (layer.uses_2_5d_projection ? "yes" : "no") + ") or mask (" +
                     (layer.mask.enabled() ? "yes" : "no") + ")";
            return nullptr;
        }
        if (!is_2d_transform(layer.transform)) {
            reason = "layer uses 3D transform";
            return nullptr;
        }
        if (layer.video_source) {
            if (!video_path.empty() && video_path != layer.video_source->path) {
                reason = "more than one video source";
                return nullptr;
            }
            video_path = layer.video_source->path;
        }
    }

    if (video_path.empty()) {
        reason = "no video source";
        return nullptr;
    }

    std::unordered_map<std::string, DirectLayerResourceEntry> layer_resources;
    std::vector<std::shared_ptr<const media::CudaImageResource>> persistent_resources;
    double watermark_load_ms = 0.0;
    double watermark_upload_ms = 0.0;

    for (const auto& layer : scene_0.layers()) {
        if (layer.video_source) continue;
        const std::string layer_name = std::string(layer.name);
        if (layer_resources.find(layer_name) != layer_resources.end()) continue;

        const auto ctx_from = make_frame_context({
            .global_time = SampleTime::from_frame(static_cast<double>(layer.from.value), rate),
            .duration = duration,
            .width = width,
            .height = height,
        });
        Scene scene_from;
        bool eval_from_ok = false;
        try {
            scene_from = compiled.composition->evaluate(ctx_from);
            eval_from_ok = true;
        } catch (...) {
            eval_from_ok = false;
        }

        const Layer* active_layer = nullptr;
        if (eval_from_ok) {
            for (const auto& l : scene_from.layers()) {
                if (l.name == layer.name) {
                    active_layer = &l;
                    break;
                }
            }
        }
        if (!active_layer) active_layer = &layer;

        if (!active_layer->nodes.empty()) {
            const auto& node = active_layer->nodes[0];
            if (node.shape.type() == ShapeType::Image) {
                const auto& image = node.shape.image();
                if (!image.path.empty()) {
                    const auto img_t0 = profiling::now();
                    auto cached = image_cache.get_or_load(image.path, image.decode_options);
                    watermark_load_ms += profiling::duration_ms(img_t0, profiling::now());
                    if (!cached || !cached->valid() || cached->gpu_rgba.empty()) {
                        reason = "overlay is not available through the canonical ImageCache: " + image.path;
                        return nullptr;
                    }
                    bool cache_hit = false;
                    double upload_ms = 0.0;
                    auto resource = video_runtime->get_or_upload_image(
                        cached->gpu_key.content_digest, image.decode_options,
                        static_cast<std::uint32_t>(cached->width),
                        static_cast<std::uint32_t>(cached->height), cached->gpu_rgba,
                        cache_hit, upload_ms, reason);
                    if (!resource) return nullptr;
                    watermark_upload_ms += upload_ms;

                    DirectLayerResourceEntry entry;
                    entry.gpu_resource = resource;
                    entry.native_width = static_cast<float>(cached->width);
                    entry.native_height = static_cast<float>(cached->height);
                    float corner_radius = 0.0f;
                    if (node.shape.type() == ShapeType::RoundedRect) {
                        corner_radius = node.shape.rounded_rect().radius;
                    }
                    entry.corner_radius = corner_radius;
                    layer_resources[layer_name] = entry;
                    persistent_resources.push_back(resource);
                }
            } else if (node.shape.type() == ShapeType::TextRun ||
                       active_layer->kind == LayerKind::Text) {
#ifdef CHRONON3D_USE_BLEND2D
                std::string text_content;
                std::string font_path;
                float font_size = 58.0f;
                Color fill_color{1.0f, 1.0f, 1.0f, 1.0f};
                Color stroke_color{0.0f, 0.0f, 0.0f, 0.0f};
                float stroke_width = 0.0f;
                bool has_bg = false;
                Color bg_color{0.05f, 0.05f, 0.09f, 0.88f};
                float bg_opacity = 0.88f;
                float bg_radius = 10.0f;
                float pad_x = 20.0f;
                float pad_y = 12.0f;
                float box_w = 0.0f;
                float box_h = 0.0f;
                std::string align = "center";

                if (node.shape.type() == ShapeType::TextRun) {
                    const auto handle = node.shape.text_run_shape_handle();
                    if (handle.value) {
                        const auto& s = *handle.value;
                        if (s.layout) {
                            text_content = s.layout->source_text;
                            if (!s.layout->font.font_path.empty()) font_path = s.layout->font.font_path;
                            if (s.layout->font_size > 0.0f) font_size = s.layout->font_size;
                            box_w = s.layout->bounds.x;
                            box_h = s.layout->bounds.y;
                        }
                        fill_color = s.paint.fill;
                        if (s.paint.stroke_enabled && s.paint.stroke_width > 0.0f) {
                            stroke_color = s.paint.stroke_color;
                            stroke_width = s.paint.stroke_width;
                        }
                    }
                }

                if (font_path.empty()) {
                    reason = "text layer has no prepared font asset: " + layer_name;
                    return nullptr;
                }

                if (node.color.a > 0.0f) {
                    fill_color = node.color;
                }

                if (text_content.empty()) {
                    text_content = layer_name;
                }

                const auto text_t0 = profiling::now();
                auto rasterized = rasterize_text_direct(
                    text_content, font_path, font_size, fill_color, stroke_color, stroke_width,
                    has_bg, bg_color, bg_opacity, bg_radius, pad_x, pad_y, box_w, box_h, align);
                watermark_load_ms += profiling::duration_ms(text_t0, profiling::now());

                bool cache_hit = false;
                double upload_ms = 0.0;
                auto resource = video_runtime->get_or_upload_image(
                    rasterized.digest, ImageDecodeOptions{},
                    static_cast<std::uint32_t>(rasterized.width),
                    static_cast<std::uint32_t>(rasterized.height), rasterized.gpu_rgba,
                    cache_hit, upload_ms, reason);
                if (!resource) return nullptr;
                watermark_upload_ms += upload_ms;

                DirectLayerResourceEntry entry;
                entry.gpu_resource = resource;
                entry.native_width = static_cast<float>(rasterized.width);
                entry.native_height = static_cast<float>(rasterized.height);
                layer_resources[layer_name] = entry;
                persistent_resources.push_back(resource);
#else
                reason = "Blend2D is required for direct text rasterization";
                return nullptr;
#endif
            }
        }
    }

    auto program = std::shared_ptr<DirectYuvProgram>(new DirectYuvProgram());
    program->video_path_ = std::move(video_path);
    program->width_ = width;
    program->height_ = height;
    program->composition_ = compiled.composition;
    program->layer_resources_ = std::move(layer_resources);
    program->persistent_resources_ = std::move(persistent_resources);
    program->scene_eval_ms_ = scene_eval_ms;
    program->watermark_load_ms_ = watermark_load_ms;
    program->watermark_upload_ms_ = watermark_upload_ms;
    return program;
#endif
}

std::shared_ptr<DirectYuvFrame> DirectYuvProgram::execute(
    media::NativeVideoFrameDecoder& decoder, Frame frame) const {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)decoder; (void)frame;
    return nullptr;
#else
    auto decoded = decoder.decode_native_frame(
        video_path_, frame, width_, height_, 0.0f);
    if (!decoded || !composition_) return nullptr;

    auto result = std::make_shared<DirectYuvFrame>();
    result->decoded = std::move(decoded);

    auto template_frame = std::make_shared<DirectYuvTemplate>();
    const auto rate = composition_->frame_rate();
    const auto context = make_frame_context({
        .global_time = SampleTime::from_frame(static_cast<double>(frame.value), rate),
        .duration = composition_->duration(),
        .width = width_,
        .height = height_,
    });

    Scene scene;
    try {
        scene = composition_->evaluate(context);
    } catch (...) {
        result->program = template_frame;
        return result;
    }

    for (const auto& layer : scene.layers()) {
        if (!layer.visible || layer.video_source) continue;
        if (frame.value < layer.from.value ||
            (layer.duration.value > 0 && frame.value >= layer.from.value + layer.duration.value)) {
            continue;
        }
        if (layer.transform.opacity <= 0.001f) continue;
        auto it = layer_resources_.find(std::string(layer.name));
        if (it == layer_resources_.end() || !it->second.gpu_resource) continue;
        const auto& entry = it->second;

        float base_w = entry.native_width;
        float base_h = entry.native_height;
        float scale_x = layer.transform.scale.x;
        float scale_y = layer.transform.scale.y;
        float cx = static_cast<float>(width_) * 0.5f + layer.transform.position.x;
        float cy = static_cast<float>(height_) * 0.5f + layer.transform.position.y;
        float opacity = std::clamp(layer.transform.opacity, 0.0f, 1.0f);

        if (!layer.nodes.empty()) {
            const auto& node = layer.nodes[0];
            scale_x *= node.world_transform.scale.x;
            scale_y *= node.world_transform.scale.y;
            cx += node.world_transform.position.x;
            cy += node.world_transform.position.y;
            opacity = std::clamp(opacity * node.world_transform.opacity, 0.0f, 1.0f);
            if (node.shape.type() == ShapeType::Image) {
                const auto& img = node.shape.image();
                if (img.size.x > 0.0f && img.size.y > 0.0f) {
                    base_w = img.size.x;
                    base_h = img.size.y;
                }
                opacity = std::clamp(opacity * img.opacity, 0.0f, 1.0f);
            }
        }

        const float half_w = (base_w * 0.5f) * scale_x;
        const float half_h = (base_h * 0.5f) * scale_y;
        float dst_x0 = cx - half_w;
        float dst_y0 = cy - half_h;
        float dst_x1 = cx + half_w;
        float dst_y1 = cy + half_h;

        if (opacity <= 0.001f || dst_x1 <= dst_x0 || dst_y1 <= dst_y0) continue;

        uint32_t res_idx = static_cast<uint32_t>(template_frame->resources.size());
        template_frame->batch.instances.push_back(runtime::LayerInstance{
            .kind = runtime::PrimitiveKind::Image,
            .resource_index = res_idx,
            .src_x0 = 0.0f, .src_y0 = 0.0f, .src_x1 = 1.0f, .src_y1 = 1.0f,
            .dst_x0 = dst_x0, .dst_y0 = dst_y0,
            .dst_x1 = dst_x1, .dst_y1 = dst_y1,
            .opacity = opacity,
            .blend = layer.blend_mode == BlendMode::Add ? BlendMode::Add : BlendMode::Normal,
            .corner_radius = entry.corner_radius});

        media::CudaLayerResource res;
        res.rgba = entry.gpu_resource->ptr;
        res.pitch_bytes = static_cast<int>(entry.gpu_resource->pitch_bytes);
        res.width = entry.gpu_resource->width;
        res.height = entry.gpu_resource->height;
        template_frame->resources.push_back(res);
        if (!template_frame->resource_owner) {
            template_frame->resource_owner = entry.gpu_resource;
        }
        template_frame->resource_owners.push_back(entry.gpu_resource);
    }

    result->program = std::move(template_frame);
    return result;
#endif
}

} // namespace chronon3d::cli

