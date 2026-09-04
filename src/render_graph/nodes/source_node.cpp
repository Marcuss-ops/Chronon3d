#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "../builder/evaluated_layer_placement.hpp"
#include "detail/preflight_bbox.hpp"
#include "native_surface.hpp"
#include "detail/producer_surface_bounds.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace chronon3d::graph {

namespace {

constexpr f32 kSeedFrameEpsilon = 1e-3f;

// Strict native ImageShape subset. Decode remains owned by ImageCache; only
// the immutable decoded pixels are promoted once into GpuAssetCache. Crop,
// rounded corners, masks and projective transforms intentionally stay on the
// reference path until equivalent native kernels exist.
[[nodiscard]] bool try_native_image_impl(
    RenderGraphContext& ctx, Framebuffer& fb,
    const RenderNode& node, const RenderState& state) {
    const auto& image = node.shape.image();
    spdlog::info("[native_image_diag] path='{}' matrix3=[{:.1f},{:.1f},{:.1f},{:.1f}] anchor=[{:.1f},{:.1f}] op={:.2f} state_op={:.2f}",
                 image.path, state.matrix[3][0], state.matrix[3][1], state.matrix[0][0], state.matrix[1][1],
                 node.world_transform.anchor.x, node.world_transform.anchor.y,
                 image.opacity, state.opacity);
    if (!ctx.services.image_cache || !ctx.services.gpu_asset_cache ||
        !ctx.services.backend || !ctx.services.surface_registry ||
        image.path.empty() || image.radius > 0.0f ||
        (state.mask && state.mask->enabled()) ||
        std::abs(state.matrix[0][1]) > 1e-4f ||
        std::abs(state.matrix[1][0]) > 1e-4f ||
        std::abs(state.matrix[0][3]) > 1e-4f ||
        std::abs(state.matrix[1][3]) > 1e-4f ||
        std::abs(state.matrix[3][3] - 1.0f) > 1e-4f) {
        return false;
    }

    auto cached = ctx.services.image_cache->find(
        image.path, image.decode_options);
    if (!cached) {
        cached = ctx.services.image_cache->get_or_load(
            image.path, image.decode_options);
    }
    if (!cached || !cached->fb_img || !cached->valid() || cached->gpu_rgba.empty()) return false;

    const auto& key = cached->gpu_key;
    const runtime::SurfaceDesc desc{
        key.width,
        key.height,
        key.format,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent,
        cached->gpu_rgba.size() * sizeof(float)};
    const auto acquired = ctx.services.gpu_asset_cache->acquire(key, desc, cached->gpu_rgba);
    if (!acquired.ok()) return false;
    if (ctx.policy.diagnostics_enabled && acquired.cache_hit) {
        spdlog::debug("[gpu-asset-cache] static image '{}' reused resident handle={}",
                      image.path, static_cast<std::uint64_t>(acquired.handle));
    }

    const auto& source = *cached->fb_img;

    if (!ensure_empty_native_surface(ctx, fb)) return false;
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
    const Vec2 source_origin = effective_source_origin + placement.src_rect.origin;
    const Vec2 source_size = placement.src_rect.size;
    if (source_size.x <= 0.0f || source_size.y <= 0.0f ||
        placement.dst_rect.size.x <= 0.0f || placement.dst_rect.size.y <= 0.0f) {
        release_native_surface(ctx, fb);
        return false;
    }

    // SourceNode's state matrix maps ImageShape local units. The placement
    // maps the selected source rectangle into the authored image box first.
    const float sx = state.matrix[0][0] * placement.dst_rect.size.x / source_size.x;
    const float sy = state.matrix[1][1] * placement.dst_rect.size.y / source_size.y;
    // ImageShape stores its local origin at the top-left of the authored
    // box, while RenderNode's anchor is the box centre.  The native affine
    // path samples from destination pixels directly and therefore must apply
    // the same anchor subtraction as the canonical image processor.  Without
    // it, a tight ROI uses the world-space centre as its left edge and the
    // image is sampled mostly outside the source surface.
    const float tx = state.matrix[3][0] - node.world_transform.anchor.x +
                     state.matrix[0][0] * placement.dst_rect.origin.x;
    const float ty = state.matrix[3][1] - node.world_transform.anchor.y +
                     state.matrix[1][1] * placement.dst_rect.origin.y;
    if (std::abs(sx) < 1e-6f || std::abs(sy) < 1e-6f) {
        release_native_surface(ctx, fb);
        return false;
    }

    runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 1.0f / sx;
    transform.source_x[2] = source_origin.x - tx / sx;
    transform.source_y[1] = 1.0f / sy;
    transform.source_y[2] = source_origin.y - ty / sy;
    transform.max_x = source_origin.x + source_size.x;
    transform.max_y = source_origin.y + source_size.y;
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
    spdlog::info("[native_image_bounds] dst=[{:.1f},{:.1f} -> {:.1f},{:.1f}] img=[{},{} -> {},{}] sx={:.3f} sy={:.3f} tx={:.1f} ty={:.1f}",
                 dst_x0, dst_y0, dst_x1, dst_y1, img_x0, img_y0, img_x1, img_y1, sx, sy, tx, ty);

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
    transform.clip_rect[0] = effective_clip_x0;
    transform.clip_rect[1] = effective_clip_y0;
    transform.clip_rect[2] = effective_clip_x1;
    transform.clip_rect[3] = effective_clip_y1;

    const auto result = ctx.services.backend->transform_surface_affine(
        fb.surface_handle(), acquired.handle, transform);
    if (!result.ok()) {
        release_native_surface(ctx, fb);
        return false;
    }
    return true;
}

[[nodiscard]] bool nearly_equal(f32 a, f32 b, f32 eps = kSeedFrameEpsilon) {
    return std::abs(a - b) <= eps;
}

[[nodiscard]] bool covers_full_frame_as_rectangle(
    const Mat4& matrix,
    f32 width,
    f32 height,
    bool centered = false
) {
    const f32 x0 = centered ? -width * 0.5f : 0.0f;
    const f32 x1 = centered ?  width * 0.5f : width;
    const f32 y0 = centered ? -height * 0.5f : 0.0f;
    const f32 y1 = centered ?  height * 0.5f : height;

    const Vec4 corners[4] = {
        matrix * Vec4(x0, y0, 0.0f, 1.0f),
        matrix * Vec4(x1, y0, 0.0f, 1.0f),
        matrix * Vec4(x1, y1, 0.0f, 1.0f),
        matrix * Vec4(x0, y1, 0.0f, 1.0f)
    };

    std::array<f32, 4> xs{};
    std::array<f32, 4> ys{};
    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();

    for (std::size_t i = 0; i < 4; ++i) {
        const auto& c = corners[i];
        if (std::abs(c.w) < 1e-6f) {
            return false;
        }

        xs[i] = c.x / c.w;
        ys[i] = c.y / c.w;
        min_x = std::min(min_x, xs[i]);
        min_y = std::min(min_y, ys[i]);
        max_x = std::max(max_x, xs[i]);
        max_y = std::max(max_y, ys[i]);
    }

    auto distinct_count = [](const std::array<f32, 4>& values) {
        std::array<f32, 4> unique{};
        std::size_t count = 0;
        for (f32 value : values) {
            bool seen = false;
            for (std::size_t i = 0; i < count; ++i) {
                if (nearly_equal(value, unique[i])) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                unique[count++] = value;
            }
        }
        return count;
    };

    if (distinct_count(xs) != 2 || distinct_count(ys) != 2) {
        return false;
    }

    return nearly_equal(min_x, 0.0f) &&
           nearly_equal(min_y, 0.0f) &&
           nearly_equal(max_x, width) &&
           nearly_equal(max_y, height);
}

// GPU solid-rect fast-path: fill an un-stroked, un-rounded, solid,
// axis-aligned rectangle directly into a native surface instead of
// rasterizing on CPU.  Returns false (leaving the framebuffer on the legacy
// draw_node path) when the backend has no surface support, the placement is
// not a pure axis-aligned affine transform, or the shape does not satisfy
// the narrow predicate — the software path is therefore unchanged.
//
// The software processor computes `node.color.to_linear()` with
// alpha *= state.opacity and source-over blends it onto the (transparent)
// framebuffer, which stores PREMULTIPLIED RGBA.  The GPU path mirrors that
// exactly: it premultiplies the same straight color and stores it into the
// surface.  Coverage matches the CPU rasterizer's pixel-center hit test for
// a solid rect: pixel (x, y) is filled iff its center maps inside [0,size).
bool try_native_rect_fill(RenderGraphContext& ctx, Framebuffer& fb,
                          const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::Rect) return false;
    if (node.shape.rect().stroke.enabled) return false;
    // Render-plan color layers carry their canonical color in RenderNode::color;
    // the builder's Fill metadata may remain at its default while the shape
    // is still an ordinary solid rectangle.  Stroke/radius/mask checks below
    // keep this native route limited to the equivalent solid operation.
    if (node.corner_radius > 0.0f) return false;
    if (state.mask && state.mask->enabled()) return false;
    if (!ctx.services.backend || !ctx.services.surface_registry) return false;
    // Axis-aligned affine placement only.  A 2D rotation/shear shows up in
    // matrix[0][1]/matrix[1][0]; a camera projection shows up in the w-row
    // entries.  A projected layer therefore falls back to draw_node while a
    // plain translation/scale (including a defer-projected local source) does
    // not.  The z-axis cross-terms only affect z and are irrelevant to a 2D
    // rect fill.
    if (std::abs(state.matrix[0][1]) > 1e-4f || std::abs(state.matrix[1][0]) > 1e-4f) return false;
    if (std::abs(state.matrix[0][3]) > 1e-4f || std::abs(state.matrix[1][3]) > 1e-4f ||
        std::abs(state.matrix[3][3] - 1.0f) > 1e-4f) return false;

    Color fill_color = node.color.to_linear();
    fill_color.a *= state.opacity;
    if (fill_color.a <= 0.0f) return false;

    // Exact pixel coverage.  compute_world_bbox() pads by kBBoxSafetyPadding
    // for the general rasterizer; the GPU fill must instead cover exactly the
    // pixels whose centers fall inside the rect, mirroring the CPU hit test.
    const auto& size = node.shape.rect().size;
    const Vec4 c00 = state.matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec4 c10 = state.matrix * Vec4(size.x, 0.0f, 0.0f, 1.0f);
    const Vec4 c01 = state.matrix * Vec4(0.0f, size.y, 0.0f, 1.0f);
    const Vec4 c11 = state.matrix * Vec4(size.x, size.y, 0.0f, 1.0f);
    const f32 min_x = std::min({c00.x, c10.x, c01.x, c11.x});
    const f32 min_y = std::min({c00.y, c10.y, c01.y, c11.y});
    const f32 max_x = std::max({c00.x, c10.x, c01.x, c11.x});
    const f32 max_y = std::max({c00.y, c10.y, c01.y, c11.y});
    i32 x0 = std::max(0, static_cast<i32>(std::ceil(min_x - 0.5f)));
    i32 y0 = std::max(0, static_cast<i32>(std::ceil(min_y - 0.5f)));
    i32 x1 = std::min(fb.width(), static_cast<i32>(std::ceil(max_x - 0.5f)));
    i32 y1 = std::min(fb.height(), static_cast<i32>(std::ceil(max_y - 0.5f)));
    if (ctx.node_exec.clip_rect) {
        x0 = std::max(x0, ctx.node_exec.clip_rect->x0);
        y0 = std::max(y0, ctx.node_exec.clip_rect->y0);
        x1 = std::min(x1, ctx.node_exec.clip_rect->x1);
        y1 = std::min(y1, ctx.node_exec.clip_rect->y1);
    }

    const bool covers_full_destination =
        x0 <= 0 && y0 <= 0 && x1 >= fb.width() && y1 >= fb.height();
    // A full-frame solid rect writes every destination pixel.  It therefore
    // needs no CPU seed: allocate an empty native surface and let the GPU
    // fill kernel produce the result.  Partial rects retain the legacy
    // promotion because their untouched pixels still carry source state.
    if (covers_full_destination) {
        if (!ensure_empty_native_surface(ctx, fb)) return false;
    } else if (!ensure_native_surface(ctx, fb, "SourceNode.rect.partial")) {
        return false;
    }

    if (x0 >= x1 || y0 >= y1) {
        return true;  // fully off-canvas: surface attached, nothing to fill
    }

    // Surfaces store premultiplied RGBA (the convention the software
    // compositor produces), so premultiply the straight fill color here.
    fill_color.r *= fill_color.a;
    fill_color.g *= fill_color.a;
    fill_color.b *= fill_color.a;

    const auto result = ctx.services.backend->fill_rect_surface(
        fb.surface_handle(), x0, y0, x1, y1, fill_color);
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

std::optional<raster::BBox> SourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    const Mat4 base_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
    const f32 opacity = m_opacity_override.value_or(m_node.world_transform.opacity);
    const bool exclude_from_projection = m_node.shape.type() == ShapeType::FakeBox3D;
    const auto placement = detail::evaluate_source_payload_placement(
        base_matrix,
        opacity,
        ctx,
        m_apply_camera_projection,
        m_defer_camera_projection,
        m_native_3d,
        m_name,
        "predicted_bbox",
        static_cast<std::size_t>(-1),
        exclude_from_projection);
    if (!placement) {
        return std::nullopt;
    }
    const Mat4 matrix = placement->render_matrix;

    f32 spread = 0.0f;
    spread += 8.0f;

    // TICKET-122 FASE 3: GridPlane now goes through 2.5D projection above,
    // so it uses the standard compute_world_bbox path (not native 3D).
    if (m_node.shape.type() == ShapeType::Mesh ||
        (m_apply_camera_projection && ctx.frame_input.has_camera_2_5d &&
         m_node.shape.type() == ShapeType::FakeBox3D)) {
        if (auto bbox = detail::projected_native_3d_bbox(
                ctx, m_node, placement->render_matrix, spread)) {
            return bbox;
        }
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    // Keep the diagnostic/world-space bounds separate from the execution
    // bounds.  Diagnostics may inspect the unclipped geometry, but culling,
    // tile pruning, dirty clipping, and cache state must always consume the
    // same canvas-clipped bbox regardless of the logging flag.
    const auto diagnostic_bbox =
        renderer::compute_world_bbox(m_node.shape, matrix, spread);
    const auto execution = detail::resolve_execution_bbox(
        *placement, diagnostic_bbox, ctx);
    if (!execution) {
        return raster::BBox{0, 0, 0, 0};
    }
    const auto execution_bbox = *execution;

    if (ctx.policy.diagnostics_enabled) {
        spdlog::debug(
            "[source-bbox] node='{}' diagnostic=[{},{},{},{}] execution=[{},{},{},{}]",
            m_name,
            diagnostic_bbox.x0, diagnostic_bbox.y0,
            diagnostic_bbox.x1, diagnostic_bbox.y1,
            execution_bbox.x0, execution_bbox.y0,
            execution_bbox.x1, execution_bbox.y1);
    }

    if (execution_bbox.is_empty()) {
        return raster::BBox{0, 0, 0, 0};
    }
    return execution_bbox;
}

std::optional<raster::BBox> detail::preflight_diagnostic_bbox(
    const SourceNode& node,
    const RenderGraphContext& ctx) {
    const Mat4 base_matrix = node.m_matrix_override.value_or(node.m_node.world_transform.to_mat4());
    const f32 opacity = node.m_opacity_override.value_or(node.m_node.world_transform.opacity);
    const bool exclude_from_projection = node.m_node.shape.type() == ShapeType::FakeBox3D;
    const auto placement = detail::evaluate_source_payload_placement(
        base_matrix, opacity, ctx, node.m_apply_camera_projection,
        node.m_defer_camera_projection, node.m_native_3d, node.m_name, "diagnostic_bbox",
        static_cast<std::size_t>(-1), exclude_from_projection);
    if (!placement) {
        return std::nullopt;
    }

    const f32 spread = 8.0f;
    if (node.m_node.shape.type() == ShapeType::Mesh ||
        (node.m_apply_camera_projection && ctx.frame_input.has_camera_2_5d &&
         node.m_node.shape.type() == ShapeType::FakeBox3D)) {
        return detail::projected_native_3d_bbox(
            ctx, node.m_node, placement->render_matrix, spread);
    }
    return renderer::compute_world_bbox(
        node.m_node.shape, placement->render_matrix, spread);
}

cache::NodeCacheKey SourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // TICKET-122: use the current evaluation frame instead of the
    // build-time frame (always Frame{0} for frame-variant nodes),
    // so the cache key differentiates between frames even when
    // params_hash alone would collide (e.g. zoom-identical states).
    key.frame = cache_frame_for_policy(cache_policy(), ctx.frame_input.frame);
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.policy.modular_coordinates));
    if (m_matrix_override) {
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
    }
    if (m_opacity_override) {
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_opacity_override), sizeof(f32)));
    }
    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B).
    // Conditional on `has_camera_2_5d` globally — AE_CAM_02 zoom-only
    // frames now produce distinct per-frame keys.
    if (ctx.frame_input.has_camera_2_5d) {
        cache::fold_camera_into_params_hash(key, ctx.frame_input.camera_2_5d);
    }
    return key;
}

NodeExecResult SourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_TRACE_SCOPE("chronon.node", "source_render");
    if (m_cache_policy.reusable_across_frames() && m_cached_result &&
        !ctx.frame_input.has_camera_2_5d &&
        (!ctx.node_exec.clip_rect || (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
                                      ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width &&
                                      ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height))) {
        return NodeExecResult{ctx.acquire_owned_fb(*m_cached_result)};
    }
    const bool full_frame_seed = can_seed_full_frame(ctx);
    const Mat4 base_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
    const f32 opacity = m_opacity_override.value_or(m_node.world_transform.opacity);

    // A screen-space solid background is semantically a clear, not a shape
    // raster.  Keep this predicate deliberately narrow: only an un-stroked,
    // solid, opaque rectangle with an affine placement that covers exactly
    // the logical canvas may take it.  Gradients, rounded corners, camera
    // projection, clips and transformed rectangles continue through the
    // general raster path.
    bool direct_full_frame_fill = false;
    if (!m_apply_camera_projection && !m_defer_camera_projection && !m_native_3d &&
        m_node.shape.type() == ShapeType::Rect &&
        !m_node.shape.rect().stroke.enabled &&
        m_node.fill.type == FillType::Solid &&
        m_node.corner_radius <= 0.0f && opacity >= 0.999f &&
        m_node.color.a >= 0.999f &&
        (!ctx.node_exec.clip_rect ||
         (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
          ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width &&
          ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height))) {
        const auto placement = detail::evaluate_source_payload_placement(
            base_matrix,
            opacity,
            ctx,
            false,
            false,
            false,
            m_name,
            "direct_full_frame_fill");
        direct_full_frame_fill = placement && covers_full_frame_as_rectangle(
            placement->render_matrix,
            static_cast<f32>(ctx.frame_input.width),
            static_cast<f32>(ctx.frame_input.height),
            false);
    }

    // Skip clear when full-frame opaque with integer translation — no
    // sub-pixel gaps are possible because the source covers every pixel
    // and the composite path uses integer-rounded coordinates.
    bool skip_clear = false;
    if (full_frame_seed) {
        const auto mat = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        // Identity scale + rotation (coefficients ~1 or ~0) and integer translation
        const bool identity_scale_rot =
            std::abs(mat[0][0] - 1.0f) < 1e-4f && std::abs(mat[0][1]) < 1e-4f &&
            std::abs(mat[1][0]) < 1e-4f && std::abs(mat[1][1] - 1.0f) < 1e-4f;
        if (identity_scale_rot) {
            const float tx = mat[3][0];
            const float ty = mat[3][1];
            skip_clear = std::abs(tx - std::round(tx)) < 1e-4f &&
                         std::abs(ty - std::round(ty)) < 1e-4f;
        }
    }

    // Only image overlays need a predicted visual box here.  Background and
    // video producers are intentionally full-canvas and must not pay for an
    // extra per-frame bounds calculation.
    std::optional<raster::BBox> source_bbox;
    if (m_node.shape.type() == ShapeType::Image && !full_frame_seed) {
        source_bbox = predicted_bbox(ctx);
    }
    const auto producer_surface = detail::resolve_producer_surface_bounds(
        ctx.frame_input.width,
        ctx.frame_input.height,
        m_node.shape.type() == ShapeType::Image
            ? detail::ProducerSurfaceKind::Image
            : detail::ProducerSurfaceKind::Background,
        source_bbox ? &*source_bbox : nullptr);
    detail::record_producer_surface(
        ctx.node_exec.counters,
        m_node.shape.type() == ShapeType::Image
            ? detail::ProducerSurfaceKind::Image
            : detail::ProducerSurfaceKind::Background,
        producer_surface,
        ctx.frame_input.width,
        ctx.frame_input.height);

    OwnedFB fb;
    if (producer_surface.tight && ctx.services.framebuffer_pool &&
        !ctx.node_exec.planned_physical_slot) {
        fb = ctx.services.framebuffer_pool->acquire_owned_exact(
            producer_surface.width(), producer_surface.height(),
            !skip_clear && !direct_full_frame_fill);
        fb->set_origin(producer_surface.bounds.x0, producer_surface.bounds.y0);
    } else {
        fb = ctx.acquire_owned_fb(
            producer_surface.width(),
            producer_surface.height(),
            !skip_clear && !direct_full_frame_fill,
            producer_surface.bounds);
    }

    // Keep the CPU clear only for the software backend.  A Vulkan video job
    // must materialize even a full-frame color layer as a native surface so
    // the final framebuffer can stay device-local all the way to NVENC.
    const bool native_surface_backend =
        ctx.services.backend && ctx.services.backend->supports_native_video_surface();
    if (direct_full_frame_fill && !native_surface_backend) {
        Color fill_color = m_node.color.to_linear();
        fill_color.a *= opacity;
        fb->clear(fill_color);
        fb->set_opaque(true);
        if (ctx.policy.diagnostics_enabled) {
            spdlog::info("[source-fast-path] node='{}' direct_full_frame_fill=1 pixels={}",
                         m_name,
                         static_cast<std::uint64_t>(ctx.frame_input.width) *
                             static_cast<std::uint64_t>(ctx.frame_input.height));
        }
        return NodeExecResult{std::move(fb)};
    }

    if (ctx.services.backend) {
        RenderState state;
        state.frame_number = static_cast<int>(ctx.frame_input.frame);
        state.ssaa_factor = ctx.policy.ssaa_factor;

        const auto placement = detail::evaluate_source_payload_placement(
            base_matrix,
            opacity,
            ctx,
            m_apply_camera_projection,
            m_defer_camera_projection,
            m_native_3d,
            m_name,
            "execute",
            static_cast<std::size_t>(-1),
            m_node.shape.type() == ShapeType::FakeBox3D);
        if (!placement) {
            if (ctx.policy.diagnostics_enabled) {
                spdlog::info(
                    "[source-skip] node='{}' proj.visible=false frame={} — returning empty fb",
                    m_name,
                    ctx.frame_input.sample_time.integral_frame());
            }
            fb->set_opaque(false);
            return NodeExecResult{std::move(fb)};
        }
        state.matrix = placement->render_matrix;
        state.opacity = opacity;
        state.shape_processor = ctx.node_exec.current_shape_processor;
        state.processor_snapshot = ctx.node_exec.processor_snapshot;
        state.world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        state.frame_number = static_cast<int>(ctx.frame_input.frame);

        state.clip_rect = ctx.node_exec.clip_rect;
        state.diagnostics_enabled = ctx.policy.diagnostics_enabled;

        if (ctx.frame_input.has_camera_2_5d) {
            state.projection  = ctx.frame_input.projection_ctx;
        }

        spdlog::info("[source_node_exec] node='{}' shape_type={} frame={}",
                     m_name, static_cast<int>(m_node.shape.type()), static_cast<int>(ctx.frame_input.frame));
        const bool native_filled = try_native_rect_fill(ctx, *fb, m_node, state);
        const bool native_image = !native_filled &&
            m_node.shape.type() == ShapeType::Image &&
            detail::try_native_image(ctx, *fb, m_node, state);
        if (!native_filled && !native_image) {
            // Generic Vulkan producers (circle, rounded rect, line, path)
            // also have native implementations, but unlike the rect/image
            // fast paths they materialize the surface inside draw_node().
            // Ensure the destination exists before dispatch so the backend
            // can select its native shape path instead of seeing a CPU-only
            // framebuffer and reporting a misleading unsupported capability.
            if (ctx.services.backend->supports_native_surfaces() &&
                ctx.services.surface_registry && !ensure_native_surface(ctx, *fb, "SourceNode.image.fallback")) {
                return NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    m_name,
                    "failed to materialize native source surface"};
            }
            const auto draw_result = ctx.services.backend->draw_node(
                *fb, m_node, state, ctx.frame_input.camera,
                ctx.frame_input.width, ctx.frame_input.height);
            if (!draw_result.ok()) {
                return NodeExecResult{NodeExecutionError{
                    draw_result.error().code,
                    m_name,
                    draw_result.error().message}};
            }
        }
        // A fully opaque source becomes translucent when the layer opacity
        // is animated below one; keep the metadata truthful so CompositeNode
        // cannot take the opaque fast path and replace the background.
        fb->set_opaque(full_frame_seed && state.opacity >= 1.0f);

        // Diagnostics: only log in debug mode without full-buffer pixel scanning
    }
    if (m_cache_policy.reusable_across_frames() && fb &&
        !ctx.frame_input.has_camera_2_5d &&
        fb->surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        // A cached CPU framebuffer must not carry a frame-scoped Vulkan
        // handle into a later cache hit.
        m_cached_result = std::make_shared<Framebuffer>(*fb);
    }
    return NodeExecResult{std::move(fb)};
}

bool SourceNode::can_seed_full_frame(const RenderGraphContext& ctx) const noexcept {
    // Frame-invariant + reusable across frames = eligible to skip the clear.
    // TICKET-ae-cam-hash-collision Soluzione B (rendering-side) — bail
    // when a camera is active even if the per-node flag is false: with
    // a 2.5D camera the screen-space "full frame" assumption no longer
    // holds (zoom changes the effective coverage), so full-frame
    // seeding would produce stale FBs that bypass the cache-key fix.
    // TICKET-TEXT-CLEANUP-8: m_uses_2_5d_projection removed.  2.5D
    // projection is now conditioned on has_camera_2_5d globally.
    if (!m_cache_policy.reusable_across_frames()
        || ctx.frame_input.has_camera_2_5d) {
        return false;
    }

    if (m_node.shape.type() != ShapeType::Image) {
        return false;
    }

    const auto& img = m_node.shape.image();
    const auto& tr = m_node.world_transform;
    const f32 opacity = m_opacity_override.value_or(tr.opacity);

    if (ctx.node_exec.clip_rect) {
        const bool clip_is_full = ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
                                  ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height;
        if (!clip_is_full) {
            return false;
        }
    }

    const bool full_size = std::abs(img.size.x - static_cast<f32>(ctx.frame_input.width)) < kSeedFrameEpsilon &&
                           std::abs(img.size.y - static_cast<f32>(ctx.frame_input.height)) < kSeedFrameEpsilon;
    const bool opaque = img.opacity >= 0.999f && opacity >= 0.999f;
    if (!full_size || !opaque) {
        return false;
    }

    const Mat4 local_matrix = m_matrix_override.value_or(tr.to_mat4());
    // TICKET-TEXT-CLEANUP-5: centering is now baked into matrix_override
    // by the source pass / refresh.  m_centered removed.
    const auto placement = detail::evaluate_source_payload_placement(
        local_matrix,
        opacity,
        ctx,
        false,
        false,
        false,
        m_name,
        "can_seed_full_frame");
    if (!placement) {
        return false;
    }

    return covers_full_frame_as_rectangle(
        placement->render_matrix,
        static_cast<f32>(ctx.frame_input.width),
        static_cast<f32>(ctx.frame_input.height),
        false);
}

} // namespace chronon3d::graph
