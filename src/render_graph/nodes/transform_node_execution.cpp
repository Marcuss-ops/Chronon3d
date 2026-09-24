#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <span>
#include <array>
#include <bit>
#include <cstring>

#include "native_surface.hpp"
#include "transform_policy.hpp"

// Internal helpers extracted into separate compilation units
#include "transform_internal.hpp"

#include "transform_geometry_detail.hpp"

namespace chronon3d::graph {
NodeExecResult TransformNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    CHRONON_TRACE_SCOPE("chronon.node", "transform_node");
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->transform_calls.fetch_add(1, std::memory_order_relaxed);
    }

    if (inputs.empty() || !inputs[0]) {
        return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height)};
    }

    const FramebufferRef& input = inputs[0];
    const auto predicted = predicted_bbox(ctx, input_bboxes);
    const raster::BBox out_bounds = predicted.value_or(raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height});
    const i32 out_w = std::max(1, out_bounds.x1 - out_bounds.x0);
    const i32 out_h = std::max(1, out_bounds.y1 - out_bounds.y0);

    const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
    const f32 opacity = m_use_matrix ? m_opacity : m_transform.opacity;

    // A projected layer can arrive here from a CPU-authored text/image
    // producer without a device handle yet.  Native transform dispatch used
    // to test the handle and then enter the GPU path with an unresolved input,
    // leaving the frame graph with no output surface. Promote the producer
    // before choosing the transform path; this is the same explicit CPU→GPU
    // bridge used by CompositeNode and EffectStackNode.
    if (ctx.policy.require_native_gpu &&
        input->surface_handle() == runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend && ctx.surface_registry &&
        ctx.backend->supports_native_surfaces() &&
        !ensure_native_surface(ctx, *input, "TransformNode.input",
                               /*synchronous_upload=*/true)) {
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            std::string(name()),
            "native transform input promotion failed"});
    }
    // ── Identity fast-path (uses cached analysis) ──────────────────────
    const bool is_identity = get_transform_type() == TransformType::Identity;

    // A native projected layer must still pass through the affine surface
    // kernel at the neutral pose.  The legacy identity copy path only knows
    // about CPU/full-frame storage and drops tight GPU surfaces when the 3D
    // animation reaches rotation=0,z=0 (the exact frame where the title
    // should be most visible).
    const bool native_projected_input =
        ctx.policy.require_native_gpu &&
        input->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend && ctx.surface_registry &&
        ctx.backend->is_native_surface_valid(input->surface_handle());
    if (is_identity && !native_projected_input &&
        std::abs(opacity - 1.0f) < 1e-6f &&
        input->width() == out_w && input->height() == out_h &&
        input->origin_x() == out_bounds.x0 && input->origin_y() == out_bounds.y0) {
        const uint64_t area = static_cast<uint64_t>(out_w) * out_h;
        if (ctx.backend && ctx.backend->counters()) {
            ctx.backend->counters()->pixels_touched.fetch_add(area, std::memory_order_relaxed);
        }
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->transform_pixels.fetch_add(area, std::memory_order_relaxed);
        }
        // Keep the input immutable.  It may be a cached framebuffer and the
        // DAG's single-consumer property does not prove exclusive ownership
        // of its pixel storage.  Swapping here contaminated cached identity
        // transforms and made a warm sequential render differ from a cold
        // single-frame render.
        auto result = ctx.acquire_owned_fb(out_w, out_h, false, out_bounds);
        if (input->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
            ctx.backend && ctx.surface_registry &&
            ctx.backend->is_native_surface_valid(input->surface_handle()) &&
            ensure_empty_native_surface(ctx, *result)) {
            const auto transformed = ctx.backend->transform_surface(
                result->surface_handle(), input->surface_handle(),
                input->origin_x() - result->origin_x(),
                input->origin_y() - result->origin_y(), opacity);
            if (transformed.ok()) {
                result->set_opaque(input->is_opaque());
                result->set_content_cleared(input->is_content_cleared());
                return NodeExecResult{std::move(result)};
            }
            release_native_surface(ctx, *result);
        }
        for (i32 y = 0; y < input->height(); ++y) {
            std::memcpy(result->pixels_row(y), input->pixels_row(y),
                        static_cast<std::size_t>(input->width()) * sizeof(Color));
        }
        result->set_origin(input->origin_x(), input->origin_y());
        result->set_opaque(input->is_opaque());
        result->set_content_cleared(input->is_content_cleared());
        return NodeExecResult{std::move(result)};
    }

    // ── Use transform scratch buffer when available — eliminates pool ───
    //    bucket misses caused by frame-to-frame bbox size variations in
    //    animated transforms (e.g. breathing scale animation).
    auto result = ctx.acquire_scratch_fb(out_w, out_h, true, out_bounds);

    // TICKET-012 — acquire_scratch_fb / acquire_owned_fb ignore the
    // `bounds` parameter today (pool only supports origin-(0,0) buffers).
    // Explicitly stamp the canvas-space origin so the clipping code below
    // (which uses canvas coordinates) can correctly map into the result
    // buffer's local pixel space.  Without this, a predicted_bbox at e.g.
    // (1037, 926) produces a result buffer at origin (0, 0) and the
    // clamp(x0, 0, width) collapses the rendering region to zero area.
    if (out_bounds.x0 != 0 || out_bounds.y0 != 0) {
        result->set_origin(out_bounds.x0, out_bounds.y0);
    }

    // ── Centering & homography ──────────────────────────────────────────
    const Mat4 dst_canvas_offset = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
    const Mat4 src_canvas_offset = glm::translate(Mat4(1.0f), Vec3(input->width() * 0.5f, input->height() * 0.5f, 0.0f));
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

    f32 x_min_src = 0.0f;
    f32 y_min_src = 0.0f;
    f32 x_max_src = static_cast<f32>(input->width());
    f32 y_max_src = static_cast<f32>(input->height());
    if (!input_bboxes.empty() && input_bboxes[0].has_value()) {
        const auto source = detail::normalize_transform_source_rect(
            *input_bboxes[0], input->origin_x(), input->origin_y(),
            input->width(), input->height());
        x_min_src = source.x0;
        y_min_src = source.y0;
        x_max_src = source.x1;
        y_max_src = source.y1;
        if (!source.valid()) {
            return NodeExecResult{std::move(result)};
        }
    }

    const f32 w_src = x_max_src - x_min_src;
    const f32 h_src = y_max_src - y_min_src;

    // Extract 3×3 homography for local_z = 0 plane
    glm::mat3 H;
    H[0][0] = pixel_model[0][0]; H[0][1] = pixel_model[0][1]; H[0][2] = pixel_model[0][3];
    H[1][0] = pixel_model[1][0]; H[1][1] = pixel_model[1][1]; H[1][2] = pixel_model[1][3];
    H[2][0] = pixel_model[3][0]; H[2][1] = pixel_model[3][1]; H[2][2] = pixel_model[3][3];

    const glm::mat3 inv_pixel_model_3x3 = glm::inverse(H);


    const auto winding = projected_quad_signed_area(pixel_model, w_src, h_src);
    const bool flipped = winding.has_value() && *winding < 0.0f;
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->projected_winding_flips.fetch_add(flipped ? 1 : 0, std::memory_order_relaxed);
    }

    // The camera contract uses screen-Y-down coordinates, so a negative
    // signed area is the normal orientation for an otherwise front-facing
    // card.  The inverse homography already preserves that orientation;
    // flipping its V column here changes the geometry-to-source mapping and
    // can sample entirely outside the input (empty cards or scanline bands).
    const glm::mat3& inv_H_corrected = inv_pixel_model_3x3;

    if (ctx.policy.diagnostics_enabled) {
#ifdef CHRONON_DEBUG_VERBOSE
        const f32 det = glm::determinant(H);
        spdlog::info(
            "[transform-debug] node='{}' det={:.6f} winding={} H=[[{:.3f},{:.3f},{:.3f}],[{:.3f},{:.3f},{:.3f}],[{:.3f},{:.3f},{:.3f}]]",
            name(),
            det,
            winding.has_value() ? (*winding < 0.0f ? "flipped" : "ok") : "invalid",
            H[0][0], H[0][1], H[0][2],
            H[1][0], H[1][1], H[1][2],
            H[2][0], H[2][1], H[2][2]
        );
        if (flipped) {
            spdlog::warn("[transform-debug] node='{}' projected quad has negative winding, corrected", name());
        }
#endif
    }

    // ── Projected quad → destination bounding box ───────────────────────
    const std::array<Vec4, 4> corners = {
        pixel_model * Vec4(x_min_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_max_src, 0, 1),
        pixel_model * Vec4(x_min_src, y_max_src, 0, 1)
    };
    const auto clipped = detail::clip_quad_against_w(corners);

    f32 min_x = 1e10f, max_x = -1e10f;
    f32 min_y = 1e10f, max_y = -1e10f;
    for (int i = 0; i < clipped.count; ++i) {
        const auto& c = clipped.vertices[i];
        f32 px = c.x / c.w;
        f32 py = c.y / c.w;
        min_x = std::min(min_x, px);
        max_x = std::max(max_x, px);
        min_y = std::min(min_y, py);
        max_y = std::max(max_y, py);
    }

    if (clipped.count < 3) {
        return NodeExecResult{std::move(result)};
    }

    const auto clipped_bounds = detail::clip_transform_bounds(
        min_x, min_y, max_x, max_y,
        result->origin_x(), result->origin_y(), result->width(), result->height(),
        ctx.node_exec.clip_rect);
    if (!clipped_bounds) return NodeExecResult{std::move(result)};
    const i32 x0 = clipped_bounds->x0;
    const i32 x1 = clipped_bounds->x1;
    const i32 y0 = clipped_bounds->y0;
    const i32 y1 = clipped_bounds->y1;

    const uint64_t area = (x1 > x0 && y1 > y0)
        ? static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0)
        : 0;

    if (ctx.backend && ctx.backend->counters()) {
        ctx.backend->counters()->pixels_touched.fetch_add(area, std::memory_order_relaxed);
    }
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->transform_pixels.fetch_add(area, std::memory_order_relaxed);
    }

    // ── Dispatch: choose the right transform path ───────────────────────

    // Fast-path 1: pure translation (identity 2×2 + no perspective)
    const bool is_pure_translation =
        std::abs(H[0][0] - 1.0f) < 1e-6f && std::abs(H[0][1]) < 1e-6f && std::abs(H[0][2]) < 1e-6f &&
        std::abs(H[1][0]) < 1e-6f && std::abs(H[1][1] - 1.0f) < 1e-6f && std::abs(H[1][2]) < 1e-6f &&
        std::abs(H[2][2] - 1.0f) < 1e-6f;

    if (is_pure_translation) {
        const f32 tx = H[2][0];
        const f32 ty = H[2][1];
        const bool is_integer_translation =
            std::abs(tx - std::round(tx)) < 1e-4f &&
            std::abs(ty - std::round(ty)) < 1e-4f;

        if (is_integer_translation) {
            const i32 itx = static_cast<i32>(std::round(tx));
            const i32 ity = static_cast<i32>(std::round(ty));

            // Native GPU path for the canonical integer-translation case.
            // The source must already be a device-local logical surface; the
            // CPU implementation remains the fallback for the first upload,
            // perspective, and filtered affine paths.
            const auto native_path = detail::resolve_transform_execution_path(
                ctx.policy.require_native_gpu,
                input->surface_handle() != runtime::kInvalidRenderSurfaceHandle,
                ctx.backend != nullptr,
                ctx.surface_registry != nullptr);
            if (native_path == detail::TransformExecutionPath::NativeSurface) {
                const runtime::SurfaceDesc desc = runtime::SurfaceDesc::make(
                    static_cast<std::uint32_t>(result->width()),
                    static_cast<std::uint32_t>(result->height()),
                    runtime::PixelFormat::Rgba32Float,
                    runtime::ResourceUsage::Storage,
                    runtime::LifetimeClass::FrameTransient);
                const auto handle = ctx.surface_registry->create(desc);
                const auto created = handle != runtime::kInvalidRenderSurfaceHandle
                    ? ctx.backend->create_surface(handle, desc)
                    : RenderOpResult(make_render_backend_error(
                        RenderBackendErrorCode::InvalidInput,
                        "TransformNode: unable to allocate native output handle"));
                if (created.ok()) {
                    const auto transformed = ctx.backend->transform_surface(
                        handle, input->surface_handle(),
                        input->origin_x() + itx - result->origin_x(),
                        input->origin_y() + ity - result->origin_y(), opacity);
                    if (transformed.ok()) {
                        result->set_surface_handle(handle);
                        return NodeExecResult{std::move(result)};
                    }
                    spdlog::warn("[transform-native] integer transform failed node='{}' source={} dest={} error='{}'",
                                 name(), input->surface_handle(), handle,
                                 transformed.error().message);
                } else {
                    spdlog::warn("[transform-native] integer output surface creation failed node='{}' error='{}'",
                                 name(), created.error().message);
                }
                if (handle != runtime::kInvalidRenderSurfaceHandle) {
                    (void)ctx.backend->release_surface(handle);
                    ctx.surface_registry->release(handle);
                }
            }
            if (ctx.policy.require_native_gpu) {
                return NodeExecResult(NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    std::string(name()),
                    "native integer transform failed"});
            }
            detail::execute_translate_clamped(
                result.get(), input.get(),
                x0, x1, y0, y1,
                x_min_src, x_max_src, y_min_src, y_max_src,
                itx, ity, opacity);
            return NodeExecResult{std::move(result)};
        }
    }

    // Fast-path 2: affine + no scale/shear (effectively translation)
    // In the homogeneous 3x3 layout used here, H[2][0]/H[2][1] are the
    // translation terms.  Affine transforms are allowed to translate; the
    // perspective terms are the third-row coefficients H[0][2]/H[1][2].
    // Checking the translation terms incorrectly classified every centred
    // scale (and ordinary non-zero translation) as projective, bypassing the
    // native affine compositor and, in strict GPU mode, failing the frame.
    const bool is_affine = std::abs(H[0][2]) < 1e-9f && std::abs(H[1][2]) < 1e-9f;

    if (is_affine && std::abs(H[0][1]) < 1e-6f && std::abs(H[1][0]) < 1e-6f &&
        std::abs(H[0][0] - 1.0f) < 1e-6f && std::abs(H[1][1] - 1.0f) < 1e-6f) {
        const float tx = H[2][0];
        const float ty = H[2][1];
        if (std::abs(tx - std::round(tx)) < 1e-4f && std::abs(ty - std::round(ty)) < 1e-4f) {
            const i32 itx = static_cast<i32>(std::round(tx));
            const i32 ity = static_cast<i32>(std::round(ty));
            detail::execute_translate_memcpy(
                result.get(), input.get(),
                x0, x1, y0, y1,
                itx, ity, opacity);
            return NodeExecResult{std::move(result)};
        }
    }

    // Native GPU path for affine and projective transforms. The inverse
    // homography is the same mapping used by execute_*_rows; the Vulkan
    // affine kernel consumes an optional denominator so 2.5D tilt/rotation
    // stays on GPU instead of falling through to a CPU readback.
    const auto affine_native_path = detail::resolve_transform_execution_path(
        ctx.policy.require_native_gpu,
        input->surface_handle() != runtime::kInvalidRenderSurfaceHandle,
        ctx.backend != nullptr,
        ctx.surface_registry != nullptr);
    if (affine_native_path == detail::TransformExecutionPath::NativeSurface) {
        runtime::SurfaceAffineTransform transform{};
        runtime::set_canvas_source_mapping(
            transform,
            inv_H_corrected[0][0], inv_H_corrected[1][0], inv_H_corrected[2][0],
            inv_H_corrected[0][1], inv_H_corrected[1][1], inv_H_corrected[2][1],
            runtime::RectF{x_min_src, y_min_src, x_max_src, y_max_src});
        transform.opacity = opacity;
        transform.bilinear = m_mode == SamplingMode::Bilinear ? 1u : 0u;
        // The 2.5D projection emits an unnormalized homography: its third row
        // is (0, 0, depth) because the projection carries camera depth in
        // [2][2]. is_affine only inspects row-2 x/y, so such matrices were
        // classified affine and the denominator dropped; the kernel then
        // sampled source ≈ canvas/(f·s) (the source's transparent corner) and
        // produced black frames. Always pass the denominator row: for a
        // normalized affine matrix it is exactly 1.0 and the divide is a no-op.
        if (!is_affine || std::abs(inv_H_corrected[2][2] - 1.0f) > 1e-6f) {
            transform.padding[0] = std::bit_cast<std::uint32_t>(inv_H_corrected[0][2]);
            transform.padding[1] = std::bit_cast<std::uint32_t>(inv_H_corrected[1][2]);
            transform.padding[2] = std::bit_cast<std::uint32_t>(inv_H_corrected[2][2]);
        }
        transform.destination_origin_x = result->origin_x();
        transform.destination_origin_y = result->origin_y();
        if (ctx.node_exec.clip_rect) {
            transform.clip_enabled = 1u;
            const runtime::SurfaceCoordinateMapper surface_mapper{
                result->origin_x(), result->origin_y()};
            const auto local_clip = surface_mapper.canvas_to_surface(runtime::RectF{
                static_cast<float>(ctx.node_exec.clip_rect->x0),
                static_cast<float>(ctx.node_exec.clip_rect->y0),
                static_cast<float>(ctx.node_exec.clip_rect->x1),
                static_cast<float>(ctx.node_exec.clip_rect->y1)});
            transform.clip_rect[0] = static_cast<std::int32_t>(local_clip.x0);
            transform.clip_rect[1] = static_cast<std::int32_t>(local_clip.y0);
            transform.clip_rect[2] = static_cast<std::int32_t>(local_clip.x1);
            transform.clip_rect[3] = static_cast<std::int32_t>(local_clip.y1);
        }

        const runtime::SurfaceDesc desc = runtime::SurfaceDesc::make(
            static_cast<std::uint32_t>(result->width()),
            static_cast<std::uint32_t>(result->height()),
            runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::FrameTransient);
        const auto handle = ctx.surface_registry->create(desc);
        const auto created = handle != runtime::kInvalidRenderSurfaceHandle
            ? ctx.backend->create_surface(handle, desc)
            : RenderOpResult(make_render_backend_error(
                RenderBackendErrorCode::InvalidInput,
                "TransformNode: unable to allocate native affine output handle"));
        if (created.ok()) {
            // A new storage image has undefined contents. The affine pass
            // writes only its clipped/source-covered rectangle, so establish
            // transparent pixels before dispatch to prevent an opaque black
            // ROI from reaching the final composite.
            const auto cleared = ctx.backend->initialize_transparent_surface(handle);
            const auto transformed = ctx.backend->transform_surface_affine(
                handle, input->surface_handle(), transform);
            if (cleared.ok() && transformed.ok()) {
                result->set_surface_handle(handle);
                return NodeExecResult{std::move(result)};
            }
            if (!cleared.ok()) {
                spdlog::warn("[transform-native] affine output clear failed node='{}' handle={} error='{}'",
                             name(), handle, cleared.error().message);
            }
            spdlog::warn("[transform-native] affine transform failed node='{}' source={} dest={} error='{}'",
                         name(), input->surface_handle(), handle,
                         transformed.error().message);
        } else {
            spdlog::warn("[transform-native] affine output surface creation failed node='{}' error='{}'",
                         name(), created.error().message);
        }
        if (handle != runtime::kInvalidRenderSurfaceHandle) {
            (void)ctx.backend->release_surface(handle);
            ctx.surface_registry->release(handle);
        }
    }

    if (ctx.policy.require_native_gpu) {
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            std::string(name()),
            "native affine/projective transform failed"});
    }

    // General path: affine or projective, row-based
    const Vec3 h_col_start = inv_H_corrected * Vec3(static_cast<f32>(x0) + 0.5f, static_cast<f32>(y0) + 0.5f, 1.0f);
    const Vec3 h_step_x = inv_H_corrected[0];
    const Vec3 h_step_y = inv_H_corrected[1];

    if (is_affine) {
        const f32 inv_z = 1.0f / h_col_start.z;
        const f32 dsx = h_step_x.x * inv_z;
        const f32 dsy = h_step_x.y * inv_z;

        auto worker = [&](i32 row_begin, i32 row_end) {
            detail::execute_affine_rows(
                result.get(), input.get(),
                x0, x1, y0,
                x_min_src, x_max_src, y_min_src, y_max_src,
                opacity, m_mode,
                h_col_start, h_step_y, inv_z, dsx, dsy,
                row_begin, row_end);
        };
        const bool use_par = (y1 - y0 >= 16 && area >= 16 * 32);
        if (ctx.node_exec.counters) {
            if (use_par) {
                ctx.node_exec.counters->used_parallel_transform.fetch_add(1, std::memory_order_relaxed);
            } else {
                ctx.node_exec.counters->skipped_transform_small.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (use_par) {
            parallel_for_tracked(
                tbb::blocked_range<i32>(y0, y1),
                [&](const tbb::blocked_range<i32>& range) { worker(range.begin(), range.end()); },
                ctx.node_exec.counters
            );
        } else {
            worker(y0, y1);
        }
    } else {
        auto worker = [&](i32 row_begin, i32 row_end) {
            detail::execute_projective_rows(
                result.get(), input.get(),
                x0, x1, y0,
                x_min_src, x_max_src, y_min_src, y_max_src,
                opacity, m_mode,
                h_col_start, h_step_x, h_step_y,
                row_begin, row_end);
        };
        const bool use_par = (y1 - y0 >= 16 && area >= 16 * 32);
        if (ctx.node_exec.counters) {
            if (use_par) {
                ctx.node_exec.counters->used_parallel_transform.fetch_add(1, std::memory_order_relaxed);
            } else {
                ctx.node_exec.counters->skipped_transform_small.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (use_par) {
            parallel_for_tracked(
                tbb::blocked_range<i32>(y0, y1),
                [&](const tbb::blocked_range<i32>& range) { worker(range.begin(), range.end()); },
                ctx.node_exec.counters
            );
        } else {
            worker(y0, y1);
        }
    }

    if (ctx.policy.diagnostics_enabled) {
        int nonzero = 0;
        for (i32 y = 0; y < result->height(); ++y) {
            const Color* row = result->pixels_row(y);
            for (i32 x = 0; x < result->width(); ++x) {
                if (row[x].a > 0.001f) {
                    ++nonzero;
                }
            }
        }

        // Print ASCII grid
        std::string ascii_grid = "";
        int grid_size = 20;
        int step_x = result->width() / grid_size;
        int step_y = result->height() / grid_size;
        if (step_x < 1) step_x = 1;
        if (step_y < 1) step_y = 1;
        for (int gy = 0; gy < grid_size; ++gy) {
            std::string line = "";
            for (int gx = 0; gx < grid_size; ++gx) {
                int px = gx * step_x;
                int py = gy * step_y;
                if (px < result->width() && py < result->height()) {
                    Color c = result->get_pixel(px, py);
                    if (c.r > 0.5f) line += "R";
                    else if (c.b > 0.5f) line += "B";
                    else if (c.a > 0.1f) line += "A";
                    else line += ".";
                }
            }
            ascii_grid += "\n" + line;
        }

        Color center_color = Color::transparent();
        i32 lx = 100 - result->origin_x();
        i32 ly = 100 - result->origin_y();
        if (lx >= 0 && lx < result->width() && ly >= 0 && ly < result->height()) {
            center_color = result->get_pixel(lx, ly);
        }
        std::string clip_str = ctx.node_exec.clip_rect ? (std::to_string(ctx.node_exec.clip_rect->x0) + "," + std::to_string(ctx.node_exec.clip_rect->y0) + "->" + std::to_string(ctx.node_exec.clip_rect->x1) + "," + std::to_string(ctx.node_exec.clip_rect->y1)) : "none";
        spdlog::info("[transform-debug] node='{}' output nonzero_pixels={} center_color=[{:.3f},{:.3f},{:.3f},{:.3f}] origin=[{},{}] size=[{},{}] x0_x1=[{},{}] y0_y1=[{},{}] clip={} inv_M=[[{:.3f},{:.3f},{:.3f}],[{:.3f},{:.3f},{:.3f}],[{:.3f},{:.3f},{:.3f}]] start=[{:.3f},{:.3f},{:.3f}] step_x=[{:.3f},{:.3f},{:.3f}] step_y=[{:.3f},{:.3f},{:.3f}] grid:{}",
                     name(), nonzero, center_color.r, center_color.g, center_color.b, center_color.a,
                     result->origin_x(), result->origin_y(), result->width(), result->height(),
                     x0, x1, y0, y1, clip_str,
                     inv_pixel_model_3x3[0][0], inv_pixel_model_3x3[0][1], inv_pixel_model_3x3[0][2],
                     inv_pixel_model_3x3[1][0], inv_pixel_model_3x3[1][1], inv_pixel_model_3x3[1][2],
                     inv_pixel_model_3x3[2][0], inv_pixel_model_3x3[2][1], inv_pixel_model_3x3[2][2],
                     h_col_start.x, h_col_start.y, h_col_start.z,
                     h_step_x.x, h_step_x.y, h_step_x.z,
                     h_step_y.x, h_step_y.y, h_step_y.z,
                     ascii_grid);
    }

    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
