// ═══════════════════════════════════════════════════════════════════════════
// source_node_native_image.cpp — native (GPU) image fast path for SourceNode.
//
// Split out of source_node.cpp: strict native ImageShape promotion.  Decode
// remains owned by ImageCache; only the immutable decoded pixels are promoted
// once into GpuAssetCache.  Crop, rounded corners, masks and projective
// transforms intentionally stay on the reference path until equivalent
// native kernels exist.
// ═══════════════════════════════════════════════════════════════════════════

#include "source_node_native.hpp"

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "detail/native_promotion.hpp"
#include "../builder/evaluated_layer_placement.hpp"
#include "detail/preflight_bbox.hpp"
#include "native_surface.hpp"
#include "detail/producer_surface_bounds.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace chronon3d::graph {

namespace {

/// Serial rounded-corner coverage over a cropped source buffer. This mirrors
/// apply_rounded_coverage() in backends/assets/image_renderer_support_detail
/// pixel-for-pixel (same clamp(r + 0.5 - dist) coverage on edge rows, same
/// 1 - sqrt(dist)/r+0.5 falloff on middle-row edges) so the GPU pre-rounded
/// crop composites identically to the software reference, which rounds the
/// crop before the transformed composite. Serial: rounding runs once per
/// distinct (crop, radius) and the result is content-cached in GpuAssetCache.
void apply_rounded_coverage_serial(Color* pixels, int w, int h, float radius) {
    if (!pixels || w <= 0 || h <= 0 || radius <= 0.0f) return;
    const float r = std::min({radius, static_cast<float>(w) * 0.5f,
                              static_cast<float>(h) * 0.5f});
    const int ix0 = static_cast<int>(r);
    const int iy0 = static_cast<int>(r);
    const int ix1 = w - ix0;
    const int iy1 = h - iy0;
    const float r_sq = (r + 0.5f) * (r + 0.5f);
    for (int y = 0; y < h; ++y) {
        Color* row = pixels + static_cast<std::size_t>(y) * w;
        const bool edge_row = (y < iy0 || y >= iy1);
        if (edge_row) {
            for (int x = 0; x < w; ++x) {
                const float fx = static_cast<float>(x) + 0.5f;
                const float fy = static_cast<float>(y) + 0.5f;
                const float dx = (fx < r) ? fx - r : ((fx > w - r) ? fx - (w - r) : 0.0f);
                const float dy = (fy < r) ? fy - r : ((fy > h - r) ? fy - (h - r) : 0.0f);
                if (dx == 0.0f && dy == 0.0f) continue;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float coverage = std::clamp(r + 0.5f - dist, 0.0f, 1.0f);
                if (coverage <= 0.0f) {
                    row[x] = Color::transparent();
                } else if (coverage < 1.0f) {
                    row[x].r *= coverage;
                    row[x].g *= coverage;
                    row[x].b *= coverage;
                    row[x].a *= coverage;
                }
            }
        } else {
            for (int x = 0; x < ix0 && x < w; ++x) {
                const float fx = static_cast<float>(x) + 0.5f;
                const float dx = fx - r;
                const float dist_sq = dx * dx;
                if (dist_sq >= r_sq) {
                    row[x] = Color::transparent();
                } else {
                    const float coverage = 1.0f - std::sqrt(dist_sq) / (r + 0.5f);
                    row[x].r *= coverage;
                    row[x].g *= coverage;
                    row[x].b *= coverage;
                    row[x].a *= coverage;
                }
            }
            for (int x = ix1; x < w; ++x) {
                const float fx = static_cast<float>(x) + 0.5f;
                const float dx = fx - (w - r);
                const float dist_sq = dx * dx;
                if (dist_sq >= r_sq) {
                    row[x] = Color::transparent();
                } else {
                    const float coverage = 1.0f - std::sqrt(dist_sq) / (r + 0.5f);
                    row[x].r *= coverage;
                    row[x].g *= coverage;
                    row[x].b *= coverage;
                    row[x].a *= coverage;
                }
            }
        }
    }
}

[[nodiscard]] bool try_native_image_impl(
    RenderGraphContext& ctx, Framebuffer& fb,
    const RenderNode& node, const RenderState& state) {
    const auto& image = node.shape.image();
    // Native image promotion has no per-image tint path. Preserve the
    // software oracle for color-ramped particle sprites and other tinted images.
    if (image.tint.r != 1.0f || image.tint.g != 1.0f ||
        image.tint.b != 1.0f || image.tint.a != 1.0f) return false;
    if (native_surface_promotion_diag_enabled()) {
        spdlog::info("[native_image_diag] path='{}' matrix3=[{:.1f},{:.1f},{:.1f},{:.1f}] anchor=[{:.1f},{:.1f}] op={:.2f} state_op={:.2f}",
                     image.path, state.matrix[3][0], state.matrix[3][1], state.matrix[0][0], state.matrix[1][1],
                     node.world_transform.anchor.x, node.world_transform.anchor.y,
                     image.opacity, state.opacity);
    }
    if (!ctx.image_cache || !ctx.gpu_asset_cache ||
        !ctx.backend || !ctx.surface_registry ||
        image.path.empty() ||
        image.crop.flip_x ||
        native_promotion::has_active_mask(state) ||
        !native_promotion::is_axis_aligned_affine(state)) {
        if (native_surface_promotion_diag_enabled()) {
        spdlog::info("[native_image_diag] rejected before cache: runtime={} image_cache={} gpu_cache={} backend={} surfaces={} path={} radius={:.1f} mask={} affine={}",
                     static_cast<const void*>(ctx.runtime),
                     ctx.image_cache != nullptr, ctx.gpu_asset_cache != nullptr,
                         ctx.backend != nullptr, ctx.surface_registry != nullptr,
                         !image.path.empty(), image.radius,
                         native_promotion::has_active_mask(state),
                         native_promotion::is_axis_aligned_affine(state));
        }
        return false;
    }

    auto& image_cache = *ctx.image_cache;
    auto& gpu_asset_cache = *ctx.gpu_asset_cache;
    auto cached = image_cache.find(
        image.path, image.decode_options);
    if (!cached) {
        cached = image_cache.get_or_load(
            image.path, image.decode_options);
    }
    if (!cached || !cached->fb_img || !cached->valid() || cached->gpu_rgba.empty()) {
        if (native_surface_promotion_diag_enabled()) {
            spdlog::info("[native_image_diag] rejected cache: cached={} fb={} valid={} gpu_rgba={}",
                         cached != nullptr, cached && cached->fb_img != nullptr,
                         cached && cached->valid(), cached && !cached->gpu_rgba.empty());
        }
        return false;
    }

    const auto& source = *cached->fb_img;

    if (!ensure_empty_native_surface(ctx, fb)) {
        if (native_surface_promotion_diag_enabled()) {
            spdlog::info("[native_image_diag] rejected destination surface: fb={}x{} handle={}",
                         fb.width(), fb.height(), static_cast<std::uint64_t>(fb.surface_handle()));
        }
        return false;
    }
    const Vec2 original_source_size{
        static_cast<float>(source.width()), static_cast<float>(source.height())};
    Vec2 effective_source_size = original_source_size;
    Vec2 effective_source_origin{0.0f, 0.0f};
    if (image.crop.enabled) {
        effective_source_size = image.crop.size * original_source_size;
        effective_source_origin = image.crop.origin * original_source_size;
    }
    const auto placement = compute_media_placement(
        effective_source_size, image.size, image.fit, image.focal_point);
    Vec2 source_origin = effective_source_origin + placement.src_rect.origin;
    const Vec2 source_size = placement.src_rect.size;
    if (source_size.x <= 0.0f || source_size.y <= 0.0f ||
        placement.dst_rect.size.x <= 0.0f || placement.dst_rect.size.y <= 0.0f) {
        release_native_surface(ctx, fb);
        return false;
    }

    // Rounded corners (shape 7 unblock): the native affine kernel has no
    // coverage unit, so round the integer crop on the CPU exactly like the
    // software reference (same crop rect, same scaled radius, same coverage
    // math) and promote the rounded crop. The crop pixel (i,j) IS the
    // original pixel (src_x+i, src_y+j), so the canvas→source mapping is
    // rebased by the integer origin below; sampling positions are unchanged.
    // The rounded upload is content-addressed (pixels + size), so it shares
    // the GpuAssetCache residency contract — no second cache.
    runtime::RenderSurfaceHandle src_handle = runtime::kInvalidRenderSurfaceHandle;
    int round_src_x = 0;
    int round_src_y = 0;
    std::vector<float> rounded_rgba;
    if (image.radius > 0.0f) {
        const int img_w = source.width();
        const int img_h = source.height();
        int src_x = static_cast<int>(std::round(source_origin.x));
        int src_y = static_cast<int>(std::round(source_origin.y));
        int src_w = static_cast<int>(std::round(source_size.x));
        int src_h = static_cast<int>(std::round(source_size.y));
        src_x = std::clamp(src_x, 0, img_w);
        src_y = std::clamp(src_y, 0, img_h);
        src_w = std::clamp(src_w, 0, img_w - src_x);
        src_h = std::clamp(src_h, 0, img_h - src_y);
        if (src_w <= 0 || src_h <= 0) {
            release_native_surface(ctx, fb);
            return true;
        }
        const float scaled_radius =
            image.radius * (static_cast<float>(src_w) / placement.dst_rect.size.x);
        std::vector<Color> crop(static_cast<std::size_t>(src_w) * src_h);
        for (int y = 0; y < src_h; ++y) {
            const Color* src_row = source.pixels_row(src_y + y) + src_x;
            Color* dst_row = crop.data() + static_cast<std::size_t>(y) * src_w;
            std::memcpy(dst_row, src_row, static_cast<std::size_t>(src_w) * sizeof(Color));
        }
        apply_rounded_coverage_serial(crop.data(), src_w, src_h, scaled_radius);
        rounded_rgba.resize(static_cast<std::size_t>(src_w) * src_h * 4);
        for (int y = 0; y < src_h; ++y) {
            const Color* src_row = crop.data() + static_cast<std::size_t>(y) * src_w;
            float* dst_row =
                rounded_rgba.data() + static_cast<std::size_t>(y) * src_w * 4;
            for (int x = 0; x < src_w; ++x) {
                dst_row[x * 4 + 0] = src_row[x].r;
                dst_row[x * 4 + 1] = src_row[x].g;
                dst_row[x * 4 + 2] = src_row[x].b;
                dst_row[x * 4 + 3] = src_row[x].a;
            }
        }
        const std::string_view rounded_bytes(
            reinterpret_cast<const char*>(rounded_rgba.data()),
            rounded_rgba.size() * sizeof(float));
        const runtime::GpuAssetKey rounded_key{
            assets::sha256_string(rounded_bytes), runtime::PixelFormat::Rgba32Float,
            static_cast<std::uint32_t>(src_w), static_cast<std::uint32_t>(src_h)};
        const runtime::SurfaceDesc rounded_desc{
            rounded_key.width,
            rounded_key.height,
            rounded_key.format,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent,
            rounded_rgba.size() * sizeof(float)};
        const auto rounded_acquired =
            gpu_asset_cache.acquire(rounded_key, rounded_desc, rounded_rgba);
        if (!rounded_acquired.ok()) {
            if (native_surface_promotion_diag_enabled()) {
                spdlog::info("[native_image_diag] rejected rounded GPU asset acquire: error='{}' bytes={} size={}x{}",
                             rounded_acquired.error,
                             rounded_desc.bytes, rounded_desc.width, rounded_desc.height);
            }
            release_native_surface(ctx, fb);
            return false;
        }
        src_handle = rounded_acquired.handle;
        round_src_x = src_x;
        round_src_y = src_y;
        source_origin.x -= static_cast<float>(src_x);
        source_origin.y -= static_cast<float>(src_y);
    } else {
        const auto& key = cached->gpu_key;
        const runtime::SurfaceDesc desc{
            key.width,
            key.height,
            key.format,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent,
            cached->gpu_rgba.size() * sizeof(float)};
        const auto acquired = gpu_asset_cache.acquire(key, desc, cached->gpu_rgba);
        if (!acquired.ok()) {
            if (native_surface_promotion_diag_enabled()) {
                spdlog::info("[native_image_diag] rejected GPU asset acquire: handle={} error='{}' bytes={} size={}x{}",
                             static_cast<std::uint64_t>(acquired.handle), acquired.error,
                             desc.bytes, desc.width, desc.height);
            }
            return false;
        }
        if (ctx.policy.diagnostics_enabled && acquired.cache_hit) {
            spdlog::debug("[gpu-asset-cache] static image '{}' reused resident handle={}",
                          image.path, static_cast<std::uint64_t>(acquired.handle));
        }
        src_handle = acquired.handle;
    }

    // SourceNode's state matrix maps ImageShape local units. The placement
    // maps the selected source rectangle into the authored image box first.
    const float sx = state.matrix[0][0] * placement.dst_rect.size.x / source_size.x;
    const float sy = state.matrix[1][1] * placement.dst_rect.size.y / source_size.y;
    // The authored box origin is the matrix translation plus the placement's
    // destination offset.  `Transform::to_mat4()` is
    // `T(position) * R * S * T(-anchor)`, so the anchor is ALREADY baked into
    // `state.matrix`: subtracting it a second time shifted every natively
    // drawn image by -anchor (half the canvas for a full-frame plate), and the
    // affine clip then kept only the top-left quadrant of the destination.
    // The canonical CPU image renderer consumes the same matrix and applies no
    // anchor subtraction, so this is the reference behaviour.
    //
    // Rounded pre-crop rebase: the promoted buffer starts at the integer crop
    // origin, so the destination mapping shifts by scale * integer origin and
    // the source origin drops its integer part. Sampling positions are
    // unchanged (crop pixel (i,j) IS original (src_x+i, src_y+j)).
    float tx = state.matrix[3][0] +
               state.matrix[0][0] * placement.dst_rect.origin.x;
    float ty = state.matrix[3][1] +
               state.matrix[1][1] * placement.dst_rect.origin.y;
    if (image.radius > 0.0f) {
        tx += sx * static_cast<float>(round_src_x);
        ty += sy * static_cast<float>(round_src_y);
    }
    if (std::abs(sx) < 1e-6f || std::abs(sy) < 1e-6f) {
        release_native_surface(ctx, fb);
        return false;
    }

    runtime::SurfaceAffineTransform transform{};
    runtime::set_canvas_source_mapping(
        transform,
        1.0f / sx, 0.0f, source_origin.x - tx / sx,
        0.0f, 1.0f / sy, source_origin.y - ty / sy,
        runtime::RectF{source_origin.x, source_origin.y,
                       source_origin.x + source_size.x,
                       source_origin.y + source_size.y});
    transform.opacity = image.opacity * state.opacity;
    transform.bilinear = 1u;
    transform.destination_origin_x = fb.origin_x();
    transform.destination_origin_y = fb.origin_y();
    const float dst_x0 = tx;
    const float dst_y0 = ty;
    const float dst_x1 = tx + sx * source_size.x;
    const float dst_y1 = ty + sy * source_size.y;

    const auto img_x0 = static_cast<std::int32_t>(std::floor(std::min(dst_x0, dst_x1)));
    const auto img_y0 = static_cast<std::int32_t>(std::floor(std::min(dst_y0, dst_y1)));
    const auto img_x1 = static_cast<std::int32_t>(std::ceil(std::max(dst_x0, dst_x1)));
    const auto img_y1 = static_cast<std::int32_t>(std::ceil(std::max(dst_y0, dst_y1)));
    if (native_surface_promotion_diag_enabled()) {
        spdlog::info("[native_image_bounds] dst=[{:.1f},{:.1f} -> {:.1f},{:.1f}] img=[{},{} -> {},{}] sx={:.3f} sy={:.3f} tx={:.1f} ty={:.1f}",
                     dst_x0, dst_y0, dst_x1, dst_y1, img_x0, img_y0, img_x1, img_y1, sx, sy, tx, ty);
    }

    std::int32_t effective_clip_x0 = img_x0;
    std::int32_t effective_clip_y0 = img_y0;
    std::int32_t effective_clip_x1 = img_x1;
    std::int32_t effective_clip_y1 = img_y1;

    if (state.clip_rect) {
        effective_clip_x0 = std::max(effective_clip_x0, state.clip_rect->x0);
        effective_clip_y0 = std::max(effective_clip_y0, state.clip_rect->y0);
        effective_clip_x1 = std::min(effective_clip_x1, state.clip_rect->x1);
        effective_clip_y1 = std::min(effective_clip_y1, state.clip_rect->y1);
    }

    if (effective_clip_x1 <= effective_clip_x0 || effective_clip_y1 <= effective_clip_y0) {
        release_native_surface(ctx, fb);
        return true;
    }

    transform.clip_enabled = 1u;
    const runtime::SurfaceCoordinateMapper surface_mapper{
        fb.origin_x(), fb.origin_y()};
    const auto local_clip = surface_mapper.canvas_to_surface(runtime::RectF{
        static_cast<float>(effective_clip_x0),
        static_cast<float>(effective_clip_y0),
        static_cast<float>(effective_clip_x1),
        static_cast<float>(effective_clip_y1)});
    transform.clip_rect[0] = static_cast<std::int32_t>(local_clip.x0);
    transform.clip_rect[1] = static_cast<std::int32_t>(local_clip.y0);
    transform.clip_rect[2] = static_cast<std::int32_t>(local_clip.x1);
    transform.clip_rect[3] = static_cast<std::int32_t>(local_clip.y1);

    const auto result = ctx.backend->transform_surface_affine(
        fb.surface_handle(), src_handle, transform);
    if (!result.ok()) {
        release_native_surface(ctx, fb);
        return false;
    }
    return true;
}

} // namespace

bool detail::try_native_image(
    RenderGraphContext& ctx, Framebuffer& fb,
    const ::chronon3d::RenderNode& node, const RenderState& state) {
    return try_native_image_impl(ctx, fb, node, state);
}

} // namespace chronon3d::graph
