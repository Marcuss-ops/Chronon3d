// ═══════════════════════════════════════════════════════════════════════════
// source_node_execute.cpp — per-frame execution for SourceNode.
//
// Split out of source_node.cpp: SourceNode::execute (frame lifecycle: cached
// result reuse, full-frame-seed / clear decisions, producer surface
// resolution, native fast-path dispatch, backend draw) and
// SourceNode::can_seed_full_frame.
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
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace chronon3d::graph::detail {

bool has_native_solid_rect_fill(const ::chronon3d::RenderNode& node) {
    return node.fill.enabled && node.fill.type == FillType::Solid;
}

} // namespace chronon3d::graph::detail

namespace chronon3d::graph {

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
    const Mat4 base_matrix = ctx.node_exec.sampled_layer
        ? detail::implicit_canvas_center_matrix(ctx) *
          ctx.node_exec.sampled_layer->matrix * m_node.world_transform.to_mat4()
        : m_matrix_override.value_or(m_node.world_transform.to_mat4());
    const f32 opacity = ctx.node_exec.sampled_layer
        ? ctx.node_exec.sampled_layer->opacity * m_node.world_transform.opacity
        : m_opacity_override.value_or(m_node.world_transform.opacity);

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
        direct_full_frame_fill = placement && detail::covers_full_frame_as_rectangle(
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
        const auto mat = base_matrix;
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

    // Only image overlays need a predicted visual box here. Background and
    // video producers are intentionally full-canvas and must not pay for an
    // extra per-frame bounds calculation.
    //
    // A frame-variant image in the native-video lane needs one additional
    // guarantee: its producer surface must keep a stable canvas-space extent
    // while Vulkan/CUDA/NVENC may still be consuming the previous frame. A
    // tight ROI changes both the logical framebuffer view and the affine
    // destination between animation samples; with an encode-bound physical
    // slot that can alias the prior submission. Keep animated native images
    // full-canvas. This is still device-local and avoids the software
    // fallback; static images retain the tight ROI optimization.
    const bool native_surface_backend =
        ctx.backend && ctx.backend_stats_api()->supports_native_video_surface();
    const bool stable_native_image_extent =
        m_node.shape.type() == ShapeType::Image &&
        native_surface_backend &&
        ctx.policy.retain_native_surface_for_video &&
        !m_cache_policy.reusable_across_frames();
    std::optional<raster::BBox> source_bbox;
    if (m_node.shape.type() == ShapeType::Image &&
        !full_frame_seed && !stable_native_image_extent) {
        source_bbox = predicted_bbox(ctx);
    }
    const auto producer_surface = detail::resolve_producer_surface_bounds(
        ctx.frame_input.width,
        ctx.frame_input.height,
        m_node.shape.type() == ShapeType::Image &&
                m_node.gpu_particle_instances.empty()
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
    if (producer_surface.tight && ctx.framebuffer_pool &&
        !ctx.node_exec.planned_physical_slot) {
        fb = ctx.framebuffer_pool->acquire_owned_exact(
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

    // Keep the CPU clear for software backend. On a Vulkan video job,
    // materialize the full-frame color directly into a native surface so
    // the final framebuffer stays device-local all the way to NVENC.
    if (direct_full_frame_fill) {
        if (native_surface_backend && ctx.surface_registry) {
            if (ensure_empty_native_surface(ctx, *fb)) {
                bool alpha_zero = false;
                const Color fill_color =
                    native_promotion::premultiply(m_node.color, opacity, &alpha_zero);
                if (!alpha_zero) {
                    const auto res = ctx.backend_surface_api()->fill_rect_surface(
                        fb->surface_handle(), 0, 0, fb->width(), fb->height(), fill_color);
                    if (res.ok()) {
                        fb->set_opaque(opacity >= 1.0f);
                        if (ctx.policy.diagnostics_enabled) {
                            spdlog::info("[source-fast-path-gpu] node='{}' direct_full_frame_fill=1 pixels={}",
                                         m_name,
                                         static_cast<std::uint64_t>(ctx.frame_input.width) *
                                             static_cast<std::uint64_t>(ctx.frame_input.height));
                        }
                        return NodeExecResult{std::move(fb)};
                    }
                }
            }
        }
        if (!native_surface_backend) {
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
    }

    if (ctx.backend) {
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
            // Invisible sources still participate in graph composites. A
            // strict GPU graph needs their transparent result represented by
            // a real native surface; returning the cleared CPU-pool framebuffer
            // here leaves a handle-less, backing-less input for CompositeNode.
            if (native_surface_backend && ctx.surface_registry &&
                !ensure_empty_native_surface(ctx, *fb)) {
                return NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    m_name,
                    "failed to materialize transparent native source surface"};
            }
            fb->set_opaque(false);
            return NodeExecResult{std::move(fb)};
        }
        state.matrix = placement->render_matrix;
        state.opacity = opacity;
        state.shape_processor = ctx.node_exec.current_shape_processor;
        state.processor_snapshot = ctx.node_exec.processor_snapshot;
        state.light_context = ctx.frame_input.light_context;
        state.rim_light = ctx.frame_input.rim_light;
        state.world_matrix = base_matrix;
        state.frame_number = static_cast<int>(ctx.frame_input.frame);

        state.clip_rect = ctx.node_exec.clip_rect;
        state.diagnostics_enabled = ctx.policy.diagnostics_enabled;

        if (ctx.frame_input.has_camera_2_5d) {
            state.projection  = ctx.frame_input.projection_ctx;
        }

        if (!m_node.gpu_particle_instances.empty() &&
            ctx.policy.require_native_gpu) {
            if (!ctx.backend ||
                !ctx.backend_stats_api()->supports_native_surfaces() ||
                !ctx.surface_registry ||
                !ctx.backend_texture_api() ||
                !ctx.execution_backend() ||
                !ctx.execution_backend()->is_batching_supported() ||
                !ensure_empty_native_surface(ctx, *fb)) {
                return NodeExecResult{NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability,
                    m_name,
                    "strict GPU particle instancing requires native Vulkan surfaces and layer batches"}};
            }

            // The stored instances are local to the emitter. Remove the
            // marker node's transform (its last CPU particle) from the
            // resolved source transform before applying each particle pose.
            const Mat4 particle_matrix = placement->render_matrix *
                glm::inverse(m_node.world_transform.to_mat4());
            const float axis_x_scale = glm::length(Vec2{
                particle_matrix[0][0], particle_matrix[1][0]});
            const float axis_y_scale = glm::length(Vec2{
                particle_matrix[0][1], particle_matrix[1][1]});
            std::shared_ptr<const CachedImage> sprite_image;
            runtime::RenderSurfaceHandle sprite_surface =
                runtime::kInvalidRenderSurfaceHandle;
            if (!m_node.gpu_particle_sprite_path.empty()) {
                if (!ctx.image_cache || !ctx.gpu_asset_cache) {
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability,
                        m_name,
                        "native particle sprites require the prepared image and GPU asset caches"}};
                }
                sprite_image = ctx.image_cache->find(
                    m_node.gpu_particle_sprite_path,
                    m_node.shape.image().decode_options);
                if (!sprite_image && ctx.asset_resolver) {
                    sprite_image = ctx.image_cache->find(
                        m_node.gpu_particle_sprite_path,
                        m_node.shape.image().decode_options, *ctx.asset_resolver);
                }
                if (!sprite_image || !sprite_image->valid() ||
                    sprite_image->gpu_rgba.empty()) {
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::InvalidInput,
                        m_name,
                        "native particle sprite was not prepared: " +
                            m_node.gpu_particle_sprite_path}};
                }
                const auto& key = sprite_image->gpu_key;
                const runtime::SurfaceDesc desc{
                    key.width, key.height, key.format,
                    runtime::ResourceUsage::Storage,
                    runtime::LifetimeClass::JobPersistent,
                    sprite_image->gpu_rgba.size() * sizeof(float)};
                const auto acquired = ctx.gpu_asset_cache->acquire(
                    key, desc, sprite_image->gpu_rgba);
                if (!acquired.ok()) {
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::ExecutionFailure,
                        m_name,
                        "native particle sprite promotion failed: " + acquired.error}};
                }
                sprite_surface = acquired.handle;
            }
            std::vector<runtime::LayerInstance> instances;
            std::vector<float> transforms(16, 0.0f);
            instances.reserve(m_node.gpu_particle_instances.size());
            for (const auto& particle : m_node.gpu_particle_instances) {
                const Vec4 position = particle_matrix * Vec4{
                    particle.position.x, particle.position.y, particle.z, 1.0f};
                const float radius_x = particle.size * 0.5f * axis_x_scale;
                const float radius_y = particle.size * 0.5f * axis_y_scale;
                if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                    !std::isfinite(radius_x) || !std::isfinite(radius_y) ||
                    radius_x <= 0.0f || radius_y <= 0.0f) {
                    continue;
                }
                runtime::LayerInstance instance;
                instance.kind = m_node.gpu_particle_sprite_path.empty()
                    ? runtime::PrimitiveKind::Particle
                    : runtime::PrimitiveKind::ParticleSprite;
                instance.resource_index = 0;
                const float sprite_size = particle.size * particle.sprite_scale;
                const float radius = sprite_size * 0.5f;
                if (sprite_image && (!std::isfinite(sprite_size) || sprite_size <= 0.0f))
                    continue;
                instance.dst_x0 = position.x - radius_x;
                instance.dst_y0 = position.y - radius_y;
                instance.dst_x1 = position.x + radius_x;
                instance.dst_y1 = position.y + radius_y;
                instance.opacity = 1.0f;
                instance.blend = BlendMode::Normal;
                const float alpha = std::clamp(particle.color.a * opacity, 0.0f, 1.0f);
                instance.tint = m_node.gpu_particle_sprite_path.empty()
                    ? Color{particle.color.r * alpha, particle.color.g * alpha,
                            particle.color.b * alpha, alpha}
                    : Color{particle.color.r, particle.color.g,
                            particle.color.b, 1.0f};
                instance.corner_radius = m_node.gpu_particle_bokeh ? 1.0f : 0.0f;
                if (sprite_image) {
                    instance.kind = runtime::PrimitiveKind::ParticleSprite;
                    instance.opacity = alpha;
                    const Mat4 local_to_canvas = particle_matrix *
                        glm::translate(Mat4(1.0f), Vec3{
                            particle.position.x, particle.position.y, particle.z}) *
                        glm::rotate(Mat4(1.0f), particle.rotation,
                                    Vec3{0.0f, 0.0f, 1.0f}) *
                        glm::translate(Mat4(1.0f), Vec3{
                            -radius, -radius, 0.0f});
                    const auto corner = [&](float x, float y) {
                        const Vec4 p = local_to_canvas * Vec4{x, y, 0.0f, 1.0f};
                        return Vec2{p.x, p.y};
                    };
                    const std::array<Vec2, 4> corners{
                        corner(0.0f, 0.0f), corner(sprite_size, 0.0f),
                        corner(0.0f, sprite_size), corner(sprite_size, sprite_size)};
                    instance.dst_x0 = instance.dst_x1 = corners[0].x;
                    instance.dst_y0 = instance.dst_y1 = corners[0].y;
                    for (const auto& p : corners) {
                        instance.dst_x0 = std::min(instance.dst_x0, p.x);
                        instance.dst_y0 = std::min(instance.dst_y0, p.y);
                        instance.dst_x1 = std::max(instance.dst_x1, p.x);
                        instance.dst_y1 = std::max(instance.dst_y1, p.y);
                    }
                    const float a00 = local_to_canvas[0][0];
                    const float a01 = local_to_canvas[1][0];
                    const float a10 = local_to_canvas[0][1];
                    const float a11 = local_to_canvas[1][1];
                    const float determinant = a00 * a11 - a01 * a10;
                    if (!std::isfinite(determinant) ||
                        std::abs(determinant) < 1e-6f) {
                        continue;
                    }
                    const float inv00 = a11 / determinant;
                    const float inv01 = -a01 / determinant;
                    const float inv10 = -a10 / determinant;
                    const float inv11 = a00 / determinant;
                    const float inv_tx = -(inv00 * local_to_canvas[3][0] +
                                           inv01 * local_to_canvas[3][1]);
                    const float inv_ty = -(inv10 * local_to_canvas[3][0] +
                                           inv11 * local_to_canvas[3][1]);
                    const auto fit = compute_media_placement(
                        Vec2{static_cast<float>(sprite_image->width),
                             static_cast<float>(sprite_image->height)},
                        Vec2{sprite_size, sprite_size}, FitMode::Cover);
                    if (fit.dst_rect.size.x <= 0.0f || fit.dst_rect.size.y <= 0.0f)
                        continue;
                    const float sx = fit.src_rect.size.x / fit.dst_rect.size.x;
                    const float sy = fit.src_rect.size.y / fit.dst_rect.size.y;
                    std::array<float, 16> transform{};
                    transform[0] = sx * inv00;
                    transform[4] = sx * inv01;
                    transform[12] = fit.src_rect.origin.x +
                        sx * (inv_tx - fit.dst_rect.origin.x);
                    transform[1] = sy * inv10;
                    transform[5] = sy * inv11;
                    transform[13] = fit.src_rect.origin.y +
                        sy * (inv_ty - fit.dst_rect.origin.y);
                    instance.transform_index = static_cast<std::uint32_t>(
                        transforms.size() / 16U);
                    transforms.insert(transforms.end(), transform.begin(), transform.end());
                }
                if (ctx.node_exec.clip_rect) {
                    instance.dst_x0 = std::max(instance.dst_x0,
                        static_cast<float>(ctx.node_exec.clip_rect->x0));
                    instance.dst_y0 = std::max(instance.dst_y0,
                        static_cast<float>(ctx.node_exec.clip_rect->y0));
                    instance.dst_x1 = std::min(instance.dst_x1,
                        static_cast<float>(ctx.node_exec.clip_rect->x1));
                    instance.dst_y1 = std::min(instance.dst_y1,
                        static_cast<float>(ctx.node_exec.clip_rect->y1));
                }
                if (instance.dst_x0 < instance.dst_x1 &&
                    instance.dst_y0 < instance.dst_y1) {
                    instances.push_back(instance);
                }
            }

            if (!instances.empty()) {
                runtime::RenderSurfaceHandle resource_handle = sprite_surface;
                if (resource_handle == runtime::kInvalidRenderSurfaceHandle) {
                    auto transparent_source = ctx.acquire_owned_fb(1, 1, false);
                    if (!transparent_source ||
                        !ensure_empty_native_surface(ctx, *transparent_source)) {
                        return NodeExecResult{NodeExecutionError{
                            RenderBackendErrorCode::ExecutionFailure,
                            m_name,
                            "failed to allocate the native particle batch source"}};
                    }
                    resource_handle = transparent_source->surface_handle();
                }
                const std::array<runtime::RenderSurfaceHandle, 1> resources{resource_handle};
                const auto rendered = ctx.backend_texture_api()->execute_layer_batch(
                    fb->surface_handle(), instances, resources, transforms, {});
                if (!rendered.ok()) {
                    return NodeExecResult{NodeExecutionError{
                        rendered.error().code, m_name,
                        "native particle instancing failed: " +
                            rendered.error().message}};
                }
            }
            fb->set_opaque(false);
            fb->set_content_cleared(true);
            return NodeExecResult{std::move(fb)};
        }

        // Formatted per-frame logging is forbidden in the hot loop (P1.0).
        // Counters and stage telemetry in the render receipt remain the
        // always-on observation surface.
        const bool native_filled = detail::try_native_rect_fill(ctx, *fb, m_node, state);
        const bool native_image = !native_filled &&
            m_node.shape.type() == ShapeType::Image &&
            detail::try_native_image(ctx, *fb, m_node, state);
        const bool native_mesh = !native_filled && !native_image &&
            detail::try_native_inline_mesh(ctx, *fb, m_node, state);
        if (!native_filled && !native_image && !native_mesh) {
            // Generic Vulkan producers (circle, rounded rect, line, path)
            // also have native implementations, but unlike the rect/image
            // fast paths they materialize the surface inside draw_node().
            // Ensure the destination exists before dispatch so the backend
            // can select its native shape path instead of seeing a CPU-only
            // framebuffer and reporting a misleading unsupported capability.
            if (ctx.backend_stats_api()->supports_native_surfaces() &&
                ctx.surface_registry && !ensure_empty_native_surface(ctx, *fb)) {
                return NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    m_name,
                    "failed to materialize native source surface"};
            }
            if (std::getenv("CHRONON3D_TYPED_DIAG") != nullptr) {
                const auto* mesh = m_node.shape.type() == ShapeType::Mesh
                    ? &m_node.shape.mesh_shape() : nullptr;
                Vec3 vmin{}, vmax{};
                if (mesh && mesh->inline_mesh) {
                    for (const auto& v : mesh->inline_mesh->vertices()) {
                        vmin.x = std::min(vmin.x, v.position.x); vmax.x = std::max(vmax.x, v.position.x);
                        vmin.y = std::min(vmin.y, v.position.y); vmax.y = std::max(vmax.y, v.position.y);
                        vmin.z = std::min(vmin.z, v.position.z); vmax.z = std::max(vmax.z, v.position.z);
                    }
                }
                std::fprintf(stderr,
                    "[typed-draw] node='%s' shape=%d verts=%zu idx=%zu opacity=%.3f pos=(%.2f,%.2f,%.2f) scale=(%.2f,%.2f) cam25d=%d bbox=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) color=(%.2f,%.2f,%.2f,%.2f) applycam=%d projready=%d\n",
                    m_name.c_str(), static_cast<int>(m_node.shape.type()),
                    mesh && mesh->inline_mesh ? mesh->inline_mesh->vertices().size() : 0,
                    mesh && mesh->inline_mesh ? mesh->inline_mesh->indices().size() : 0,
                    static_cast<double>(state.opacity),
                    static_cast<double>(m_node.world_transform.position.x),
                    static_cast<double>(m_node.world_transform.position.y),
                    static_cast<double>(m_node.world_transform.position.z),
                    static_cast<double>(m_node.world_transform.scale.x),
                    static_cast<double>(m_node.world_transform.scale.y),
                    ctx.frame_input.has_camera_2_5d ? 1 : 0,
                    static_cast<double>(vmin.x), static_cast<double>(vmin.y), static_cast<double>(vmin.z),
                    static_cast<double>(vmax.x), static_cast<double>(vmax.y), static_cast<double>(vmax.z),
                    static_cast<double>(m_node.color.r), static_cast<double>(m_node.color.g),
                    static_cast<double>(m_node.color.b), static_cast<double>(m_node.color.a),
                    m_apply_camera_projection ? 1 : 0,
                    ctx.frame_input.projection_ctx.ready ? 1 : 0);
                std::fprintf(stderr,
                    "[typed-state] node='%s' proj_ready=%d focal=%.2f cam=(%.2f,%.2f,%.2f) wm=[%.2f,%.2f,%.2f | %.2f,%.2f,%.2f] m=[%.2f,%.2f,%.2f | %.2f,%.2f,%.2f] clip=(%d,%d,%d,%d) matrix3d=%d\n",
                    m_name.c_str(), state.projection.ready ? 1 : 0,
                    static_cast<double>(state.projection.focal),
                    static_cast<double>(ctx.frame_input.camera.transform.position.x),
                    static_cast<double>(ctx.frame_input.camera.transform.position.y),
                    static_cast<double>(ctx.frame_input.camera.transform.position.z),
                    static_cast<double>(state.world_matrix[3][0]),
                    static_cast<double>(state.world_matrix[3][1]),
                    static_cast<double>(state.world_matrix[3][2]),
                    static_cast<double>(state.world_matrix[0][0]),
                    static_cast<double>(state.world_matrix[1][1]),
                    static_cast<double>(state.world_matrix[2][2]),
                    static_cast<double>(state.matrix[3][0]),
                    static_cast<double>(state.matrix[3][1]),
                    static_cast<double>(state.matrix[3][2]),
                    static_cast<double>(state.matrix[0][0]),
                    static_cast<double>(state.matrix[1][1]),
                    static_cast<double>(state.matrix[2][2]),
                    state.clip_rect ? static_cast<i32>(state.clip_rect->x0) : -1,
                    state.clip_rect ? static_cast<i32>(state.clip_rect->y0) : -1,
                    state.clip_rect ? static_cast<i32>(state.clip_rect->x1) : -1,
                    state.clip_rect ? static_cast<i32>(state.clip_rect->y1) : -1,
                    state.uses_2_5d_projection ? 1 : 0);
            }
            const auto draw_result = ctx.backend_frame_api()->draw_node(
                *fb, m_node, state, ctx.frame_input.camera,
                ctx.frame_input.width, ctx.frame_input.height);
            if (!draw_result.ok()) {
                return NodeExecResult{NodeExecutionError{
                    draw_result.error().code,
                    m_name,
                    draw_result.error().message}};
            }
            if (std::getenv("CHRONON3D_TYPED_DIAG") != nullptr && fb->data()) {
                std::size_t nz = 0;
                for (int yy = 0; yy < fb->height(); ++yy) {
                    const Color* row = fb->pixels_row(yy);
                    for (int xx = 0; xx < fb->width(); ++xx) {
                        if (row[xx].r != 0.0f || row[xx].g != 0.0f ||
                            row[xx].b != 0.0f || row[xx].a != 0.0f) {
                            ++nz;
                        }
                    }
                }
                std::fprintf(stderr,
                    "[typed-postdraw] node='%s' nonzero=%zu\n",
                    m_name.c_str(), nz);
            }
        }
        if (std::getenv("CHRONON3D_PATH_DIAG"))
            spdlog::info("[native-source-diag] frame={} node={} shape_type={} surface={} cpu={} opaque={} size={}x{}",
                static_cast<int>(ctx.frame_input.frame), m_name,
                static_cast<int>(m_node.shape.type()), fb->surface_handle(),
                fb->data() != nullptr, fb->is_opaque(), fb->width(), fb->height());
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
    // A full-canvas PNG cutout must leave the background subtree alive. Use
    // only the resolver-scoped prepared decode; never load or resolve assets
    // from graph-build analysis, and never infer opacity from shape metadata.
    if (m_node.shape.type() != ShapeType::Image ||
        ctx.frame_input.has_camera_2_5d || m_defer_camera_projection ||
        m_native_3d || !ctx.image_cache ||
        !ctx.asset_resolver || ctx.frame_input.width <= 0 ||
        ctx.frame_input.height <= 0) {
        return false;
    }
    if (ctx.node_exec.clip_rect &&
        (ctx.node_exec.clip_rect->x0 > 0 || ctx.node_exec.clip_rect->y0 > 0 ||
         ctx.node_exec.clip_rect->x1 < ctx.frame_input.width ||
         ctx.node_exec.clip_rect->y1 < ctx.frame_input.height)) {
        return false;
    }

    const auto& image = m_node.shape.image();
    const Mat4 source_matrix = ctx.node_exec.sampled_layer
        ? detail::implicit_canvas_center_matrix(ctx) *
          ctx.node_exec.sampled_layer->matrix * m_node.world_transform.to_mat4()
        : m_matrix_override.value_or(m_node.world_transform.to_mat4());
    const f32 opacity = ctx.node_exec.sampled_layer
        ? ctx.node_exec.sampled_layer->opacity * m_node.world_transform.opacity
        : m_opacity_override.value_or(m_node.world_transform.opacity);
    if (image.size.x != static_cast<f32>(ctx.frame_input.width) ||
        image.size.y != static_cast<f32>(ctx.frame_input.height) ||
        !std::isfinite(image.opacity) || !std::isfinite(opacity) ||
        image.opacity < 1.0f || opacity < 1.0f ||
        !std::isfinite(image.tint.a) || image.tint.a < 1.0f ||
        image.radius != 0.0f || image.crop.enabled || image.fit == FitMode::Contain ||
        image.fit == FitMode::None) {
        return false;
    }

    try {
        const auto decoded = ctx.image_cache->find(
            image.path, image.decode_options, *ctx.asset_resolver);
        if (!decoded || !decoded->decoded_alpha_is_opaque) return false;

        const auto placement = detail::evaluate_source_payload_placement(
            source_matrix, opacity, ctx, m_apply_camera_projection,
            m_defer_camera_projection, m_native_3d, m_name,
            "full_frame_seed_alpha_coverage");
        if (!placement || !detail::covers_full_frame_as_rectangle(
                placement->render_matrix,
                static_cast<f32>(ctx.frame_input.width),
                static_cast<f32>(ctx.frame_input.height), false)) {
            return false;
        }

        // Match execute()'s no-gap predicate: only identity orientation and
        // integer translation can omit the source-surface clear.
        const auto& matrix = placement->render_matrix;
        const bool identity_scale_rot =
            std::abs(matrix[0][0] - 1.0f) < 1e-4f &&
            std::abs(matrix[0][1]) < 1e-4f &&
            std::abs(matrix[1][0]) < 1e-4f &&
            std::abs(matrix[1][1] - 1.0f) < 1e-4f;
        if (!identity_scale_rot) return false;
        return std::abs(matrix[3][0] - std::round(matrix[3][0])) < 1e-4f &&
               std::abs(matrix[3][1] - std::round(matrix[3][1])) < 1e-4f;
    } catch (...) {
        // can_seed_full_frame is noexcept; cache/resolver lookup failures must
        // disable the optimization and preserve the normal clear path.
        return false;
    }
}

} // namespace chronon3d::graph
