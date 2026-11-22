#include <chronon3d/render_graph/nodes/lighting_node.hpp>

#include "native_surface.hpp"

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/rendering/image_lighting.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d::graph {

NodeExecResult LightingNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes) {
    if (inputs.empty() || !inputs[0]) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    const auto& light_context = ctx.frame_input.light_context;
    const bool has_layer_lights = light_context.has_point_lights();
    const bool per_pixel_lights = light_context.has_spatial_lights();
    if (ctx.policy.require_native_gpu && per_pixel_lights &&
        light_context.point_lights.size() > 64) {
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::UnsupportedCapability,
            std::string(name()),
            "strict-native per-pixel lighting supports at most 64 point/spot lights"});
    }
    if ((!light_context.enabled && !has_layer_lights) || !m_material.accepts_lights) {
        return ctx.acquire_owned_fb(*inputs[0]);
    }
    const bool has_native_surface =
        ctx.backend && ctx.surface_registry &&
        inputs[0]->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend_surface_api()->is_native_surface_valid(inputs[0]->surface_handle());
    if (per_pixel_lights && !inputs[0]->data() && !has_native_surface && !ctx.policy.require_native_gpu) {
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            std::string(name()),
            "per-pixel point lights require CPU pixel backing"});
    }

    // 2.5D image relighting is image-derived: the synthetic normal comes from
    // the source luminance and the rim from the source alpha. Dispatch on GPU
    // when native surfaces are active (unless light wrap or gobo texture needs CPU).
    const bool needs_edge = m_material.image_rim_strength > 0.0f ||
                            m_material.image_light_wrap_strength > 0.0f;
    const bool needs_wrap = m_material.image_light_wrap_strength > 0.0f;
    const bool image_relight = m_material.image_bump_strength > 0.0f ||
                               m_material.image_rim_strength > 0.0f ||
                               needs_wrap ||
                               !m_material.image_gobo_asset.empty();
    const bool can_gpu_relight =
        m_material.image_gobo_asset.empty() &&
        (!per_pixel_lights || light_context.point_lights.size() <= 64) &&
        (ctx.policy.require_native_gpu || (ctx.backend && ctx.backend_stats_api()->supports_native_surfaces())) &&
        has_native_surface;
    if (image_relight && !can_gpu_relight) {
        if (!inputs[0]->data()) {
            return NodeExecResult(NodeExecutionError{
                RenderBackendErrorCode::UnsupportedCapability,
                std::string(name()),
                "2.5D image relighting requires CPU pixel backing on layer '" +
                    m_layer_name + "'"});
        }
        auto result = ctx.acquire_owned_fb(*inputs[0]);
        rendering::ImageTexelView source;
        source.pixels = inputs[0]->data();
        source.width = inputs[0]->width();
        source.height = inputs[0]->height();
        source.stride = static_cast<i32>(inputs[0]->stride());

        rendering::ImageLightingParams lighting_params;
        lighting_params.bump_strength = m_material.image_bump_strength;
        lighting_params.rim_strength = m_material.image_rim_strength;
        lighting_params.rim_width_px = m_material.image_rim_width_px;
        lighting_params.roughness = m_material.roughness.value_or(0.65f);
        lighting_params.specular_intensity = m_material.specular;
        lighting_params.diffuse = m_material.diffuse;
        lighting_params.ambient_multiplier = m_material.ambient_multiplier;
        lighting_params.gobo_scale = m_material.image_gobo_scale;
        lighting_params.gobo_offset = m_material.image_gobo_offset;
        lighting_params.light_wrap_strength =
            m_material.image_light_wrap_strength;

        const auto texture_error = [&](std::string message) {
            return NodeExecResult(NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                std::string(name()), std::move(message)});
        };
        const auto load_texture = [&](const std::string& asset)
            -> std::shared_ptr<const CachedImage> {
            if (!ctx.image_cache || asset.empty()) return {};
            const ImageDecodeOptions options{};
            auto cached = ctx.asset_resolver
                ? ctx.image_cache->find(asset, options, *ctx.asset_resolver)
                : ctx.image_cache->find(ctx.resolve_asset(asset), options);
            if (!cached) {
                cached = ctx.asset_resolver
                    ? ctx.image_cache->get_or_load(asset, options, *ctx.asset_resolver)
                    : ctx.image_cache->get_or_load(ctx.resolve_asset(asset), options);
            }
            if (!cached || !cached->fb_img ||
                cached->width <= 0 || cached->height <= 0)
                return {};
            return cached;
        };

        std::shared_ptr<const CachedImage> gobo_image;
        rendering::ImageTexelView gobo_view;
        if (!m_material.image_gobo_asset.empty()) {
            gobo_image = load_texture(m_material.image_gobo_asset);
            if (!gobo_image)
                return texture_error(
                    "2.5D image lighting could not load gobo asset '" +
                    m_material.image_gobo_asset + "' for layer '" +
                    m_layer_name + "'");
            gobo_view.pixels = gobo_image->fb_img->data();
            gobo_view.width = gobo_image->width;
            gobo_view.height = gobo_image->height;
            gobo_view.stride = static_cast<i32>(gobo_image->fb_img->stride());
            if (!gobo_view.valid())
                return texture_error(
                    "2.5D image lighting gobo asset has no CPU pixels on layer '" +
                    m_layer_name + "'");
            lighting_params.gobo = &gobo_view;
        }

        std::shared_ptr<const CachedImage> wrap_image;
        rendering::ImageTexelView wrap_view;
        std::vector<Color> blurred_wrap;
        if (needs_wrap) {
            if (m_material.image_light_wrap_asset.empty())
                return texture_error(
                    "2.5D image lighting light wrap requires a background asset "
                    "on layer '" + m_layer_name + "'");
            wrap_image = load_texture(m_material.image_light_wrap_asset);
            if (!wrap_image)
                return texture_error(
                    "2.5D image lighting could not load light-wrap asset '" +
                    m_material.image_light_wrap_asset + "' for layer '" +
                    m_layer_name + "'");
            wrap_view.pixels = wrap_image->fb_img->data();
            wrap_view.width = wrap_image->width;
            wrap_view.height = wrap_image->height;
            wrap_view.stride = static_cast<i32>(wrap_image->fb_img->stride());
            if (!wrap_view.valid())
                return texture_error(
                    "2.5D image lighting light-wrap asset has no CPU pixels on "
                    "layer '" + m_layer_name + "'");
            const i32 wrap_radius = std::clamp(
                static_cast<i32>(std::lround(std::max(
                    m_material.image_rim_width_px, 1.0f))), 1, 64);
            blurred_wrap = rendering::blur_image_box(wrap_view, wrap_radius);
        }

        std::vector<f32> blurred_alpha;
        if (needs_edge) {
            const i32 radius = std::clamp(
                static_cast<i32>(std::lround(std::max(
                    m_material.image_rim_width_px, 1.0f))),
                1, 64);
            blurred_alpha = rendering::blur_alpha_box(source, radius);
        }

        const f32 layer_z = m_world_matrix[3][2];
        for (i32 y = 0; y < result->height(); ++y) {
            Color* row = result->pixels_row(y);
            const f32 pixel_y = static_cast<f32>(result->origin_y() + y) + 0.5f;
            for (i32 x = 0; x < result->width(); ++x) {
                const Color base = row[x];
                if (base.a <= 0.0f) continue;

                const Vec3 local_normal = rendering::compute_image_normal(
                    source, x, y, m_material.image_bump_strength);
                rendering::ImageLightingSample sample;
                sample.albedo = base;
                sample.normal_world = rendering::transform_normal(
                    m_world_matrix, local_normal);
                sample.pixel_world = Vec3{
                    static_cast<f32>(result->origin_x() + x) + 0.5f,
                    pixel_y, layer_z};
                sample.coverage = base.a;
                if (!blurred_alpha.empty()) {
                    const auto edge = rendering::sample_alpha_edge(
                        blurred_alpha, source.width, source.height, x, y, base.a);
                    sample.edge_outward = edge.outward;
                    sample.edge_mask = edge.mask;
                }
                if (wrap_view.valid()) {
                    const f32 canvas_w = ctx.frame_input.width > 0
                        ? static_cast<f32>(ctx.frame_input.width)
                        : static_cast<f32>(result->width());
                    const f32 canvas_h = ctx.frame_input.height > 0
                        ? static_cast<f32>(ctx.frame_input.height)
                        : static_cast<f32>(result->height());
                    const i32 bg_x = std::clamp(static_cast<i32>(
                        (static_cast<f32>(result->origin_x() + x) + 0.5f) *
                        static_cast<f32>(wrap_view.width) / canvas_w),
                        0, wrap_view.width - 1);
                    const i32 bg_y = std::clamp(static_cast<i32>(
                        (static_cast<f32>(result->origin_y() + y) + 0.5f) *
                        static_cast<f32>(wrap_view.height) / canvas_h),
                        0, wrap_view.height - 1);
                    sample.background_wrap = blurred_wrap.empty()
                        ? wrap_view.at_clamped(bg_x, bg_y)
                        : blurred_wrap[static_cast<std::size_t>(bg_y) *
                                      static_cast<std::size_t>(wrap_view.width) +
                                      static_cast<std::size_t>(bg_x)];
                }
                row[x] = rendering::evaluate_image_lighting(
                    sample, light_context, lighting_params,
                    &ctx.frame_input.rim_light);
            }
        }
        result->clear_surface_handle();
        result->mark_cpu_authoritative();
        return NodeExecResult{std::move(result)};
    }

    const Vec3 normal_world = rendering::transform_normal(
        m_world_matrix, {0.0f, 0.0f, 1.0f});
    // evaluate_lighting() is constant for a flat 2.5D layer. Evaluate the
    // exact CPU contract once for white, then apply that multiplier in a
    // Vulkan compute pass to every non-transparent pixel. This preserves the
    // CPU look without reading a GPU-only framebuffer back into RAM.
    const f32 layer_z = m_world_matrix[3][2];
    const Color multiplier = rendering::evaluate_lighting_at(
        Color{1.0f, 1.0f, 1.0f, 1.0f}, normal_world,
        Vec3{0.5f, 0.5f, layer_z}, light_context, m_material);
    static const bool path_diag_env = std::getenv("CHRONON3D_PATH_DIAG") != nullptr;
    if (path_diag_env) {
        spdlog::info(
            "[lighting-eval] layer={} enabled={} ambient_enabled={} directional_enabled={} point_lights={} normal=({:.4f},{:.4f},{:.4f}) multiplier=({:.4f},{:.4f},{:.4f},{:.4f}) material_accepts={} diffuse={:.4f}",
            m_layer_name, light_context.enabled,
            light_context.ambient_enabled, light_context.directional_enabled,
            light_context.point_lights.size(), normal_world.x, normal_world.y,
            normal_world.z, multiplier.r, multiplier.g, multiplier.b, multiplier.a,
            m_material.accepts_lights, m_material.diffuse);
    }

    // A constant multiplier is only exact for a flat scene light. Per-pixel
    // point lights vary across the layer, so they force the CPU path.
    const bool native_input =
        (!per_pixel_lights || light_context.point_lights.size() <= 64) &&
        (ctx.policy.require_native_gpu || (ctx.backend && ctx.backend_stats_api()->supports_native_surfaces())) &&
        ctx.backend && ctx.surface_registry &&
        inputs[0]->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend_surface_api()->is_native_surface_valid(inputs[0]->surface_handle());
    if (ctx.policy.require_native_gpu && per_pixel_lights && !native_input) {
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::UnsupportedCapability,
            std::string(name()),
            "strict-native per-pixel lighting requires a valid GPU surface"});
    }
    if (native_input) {
        auto result = ctx.acquire_owned_fb(
            inputs[0]->width(), inputs[0]->height(), false,
            input_bboxes.empty() ? std::nullopt : input_bboxes[0]);
        result->set_origin(inputs[0]->origin_x(), inputs[0]->origin_y());
        const bool path_diag = path_diag_env;
        if (path_diag) {
            spdlog::info(
                "[lighting-surface] layer={} phase=before-clear result_ptr={} fb={}x{} handle={} encode_handle={} slot_ptr={} input={}x{}",
                m_layer_name, static_cast<const void*>(result.get()),
                result->width(), result->height(), result->surface_handle(),
                ctx.policy.native_video_encode_surface,
                static_cast<const void*>(ctx.node_exec.planned_physical_slot),
                inputs[0]->width(), inputs[0]->height());
        }
        if (!ensure_empty_native_surface(ctx, *result)) {
            return NodeExecResult(NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                std::string(name()),
                "native lighting output promotion failed"});
        }
        if (path_diag) {
            const auto output_record = ctx.surface_registry->lookup(result->surface_handle());
            spdlog::info(
                "[lighting-surface] layer={} phase=after-clear result_ptr={} fb={}x{} handle={} registry={}x{}",
                m_layer_name, static_cast<const void*>(result.get()),
                result->width(), result->height(), result->surface_handle(),
                output_record ? output_record->desc.width : 0,
                output_record ? output_record->desc.height : 0);
        }
        runtime::RenderSurfaceHandle light_wrap_surface =
            runtime::kInvalidRenderSurfaceHandle;
        Vec4 light_wrap_params{0.0f};
        Vec4 light_wrap_raster{0.0f};
        if (image_relight && needs_wrap) {
            if (m_material.image_light_wrap_asset.empty()) {
                release_native_surface(ctx, *result);
                return NodeExecResult(NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability,
                    std::string(name()),
                    "2.5D image lighting light wrap requires a background asset "
                    "on layer '" + m_layer_name + "'"});
            }
            const auto load_wrap = [&](const std::string& asset)
                -> std::shared_ptr<const CachedImage> {
                if (!ctx.image_cache || asset.empty()) return {};
                const ImageDecodeOptions options{};
                auto cached = ctx.asset_resolver
                    ? ctx.image_cache->find(asset, options, *ctx.asset_resolver)
                    : ctx.image_cache->find(ctx.resolve_asset(asset), options);
                if (!cached) {
                    cached = ctx.asset_resolver
                        ? ctx.image_cache->get_or_load(asset, options, *ctx.asset_resolver)
                        : ctx.image_cache->get_or_load(ctx.resolve_asset(asset), options);
                }
                return cached;
            };
            auto wrap_image = load_wrap(m_material.image_light_wrap_asset);
            if (!wrap_image || !wrap_image->fb_img ||
                wrap_image->width <= 0 || wrap_image->height <= 0) {
                release_native_surface(ctx, *result);
                return NodeExecResult(NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    std::string(name()),
                    "2.5D image lighting could not load light-wrap asset '" +
                    m_material.image_light_wrap_asset + "' for layer '" +
                    m_layer_name + "'"});
            }
            rendering::ImageTexelView wrap_view{
                .pixels = wrap_image->fb_img->data(),
                .width = wrap_image->width,
                .height = wrap_image->height,
                .stride = static_cast<i32>(wrap_image->fb_img->stride())};
            if (!wrap_view.valid() || !ctx.gpu_asset_cache) {
                release_native_surface(ctx, *result);
                return NodeExecResult(NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability,
                    std::string(name()),
                    "2.5D image lighting light wrap requires a GPU asset cache "
                    "on layer '" + m_layer_name + "'"});
            }
            const i32 wrap_radius = std::clamp(
                static_cast<i32>(std::lround(std::max(
                    m_material.image_rim_width_px, 1.0f))), 1, 64);
            // Identity is the decoded asset digest plus the filter radius.
            // The blurred payload is produced only on a cache miss, so a
            // steady-state frame performs neither a CPU blur nor an upload.
            std::string wrap_key_material(
                reinterpret_cast<const char*>(wrap_image->gpu_key.content_digest.bytes.data()),
                wrap_image->gpu_key.content_digest.bytes.size());
            wrap_key_material += ':';
            wrap_key_material += std::to_string(wrap_radius);
            const runtime::GpuAssetKey wrap_key{
                assets::sha256_string(wrap_key_material),
                runtime::PixelFormat::Rgba32Float,
                static_cast<std::uint32_t>(wrap_view.width),
                static_cast<std::uint32_t>(wrap_view.height)};
            auto wrap_residency = ctx.gpu_asset_cache->find(wrap_key);
            if (!wrap_residency.ok()) {
                const auto blurred_wrap = rendering::blur_image_box(
                    wrap_view, wrap_radius);
                const runtime::SurfaceDesc wrap_desc{
                    wrap_key.width,
                    wrap_key.height,
                    wrap_key.format,
                    runtime::ResourceUsage::Storage,
                    runtime::LifetimeClass::JobPersistent,
                    blurred_wrap.size() * sizeof(Color)};
                wrap_residency = ctx.gpu_asset_cache->acquire(
                    wrap_key, wrap_desc,
                    std::span<const float>(
                        reinterpret_cast<const float*>(blurred_wrap.data()),
                        blurred_wrap.size() * 4));
            }
            if (!wrap_residency.ok()) {
                release_native_surface(ctx, *result);
                return NodeExecResult(NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability,
                    std::string(name()),
                    "2.5D image lighting light wrap could not promote its "
                    "filtered plate: " + wrap_residency.error});
            }
            light_wrap_surface = wrap_residency.handle;
            const f32 canvas_w = ctx.frame_input.width > 0
                ? static_cast<f32>(ctx.frame_input.width)
                : static_cast<f32>(result->width());
            const f32 canvas_h = ctx.frame_input.height > 0
                ? static_cast<f32>(ctx.frame_input.height)
                : static_cast<f32>(result->height());
            light_wrap_params = Vec4{
                m_material.image_light_wrap_strength,
                static_cast<f32>(wrap_view.width),
                static_cast<f32>(wrap_view.height),
                canvas_w};
            light_wrap_raster = Vec4{
                canvas_h, static_cast<f32>(wrap_radius), 0.0f, 0.0f};
        }
        RenderOpResult applied = [&] {
            if (!per_pixel_lights && !image_relight) {
                return ctx.backend_surface_api()->lighting_surface(
                    result->surface_handle(), inputs[0]->surface_handle(), multiplier);
            }
            std::vector<float> packed_lights;
            packed_lights.reserve(light_context.point_lights.size() * 16);
            for (const auto& light : light_context.point_lights) {
                packed_lights.insert(packed_lights.end(), {
                    light.position.x, light.position.y, light.position.z,
                    static_cast<float>(light.kind),
                    light.direction.x, light.direction.y, light.direction.z, light.angle,
                    light.color.r, light.color.g, light.color.b, light.radius,
                    light.range, light.decay, light.penumbra,
                    static_cast<float>(light.falloff)});
            }
            const Color scene_multiplier = light_context.enabled
                ? rendering::evaluate_lighting(
                    Color{1.0f, 1.0f, 1.0f, 1.0f}, normal_world,
                    light_context, m_material)
                : Color{0.0f, 0.0f, 0.0f, 1.0f};
            const Vec4 relight_params = image_relight
                ? Vec4{m_material.image_bump_strength,
                       m_material.image_rim_strength,
                       m_material.image_rim_width_px,
                       m_material.roughness.value_or(0.65f)}
                : Vec4{0.0f};
            const Vec4 relight_specular = image_relight
                ? Vec4{m_material.specular, 2.0f, 1.0f, 0.0f}
                : Vec4{0.0f};
            return ctx.backend_surface_api()->lighting_surface_points(
                result->surface_handle(), inputs[0]->surface_handle(),
                scene_multiplier, packed_lights,
                static_cast<std::uint32_t>(light_context.point_lights.size()),
                normal_world, layer_z, inputs[0]->origin_x(),
                inputs[0]->origin_y(), m_material.diffuse,
                relight_params, relight_specular,
                light_wrap_surface, light_wrap_params, light_wrap_raster);
        }();
        if (applied.ok()) {
            result->set_opaque(inputs[0]->is_opaque());
            result->set_content_cleared(inputs[0]->is_content_cleared());
            result->mark_gpu_authoritative();
            return NodeExecResult{std::move(result)};
        }
        const auto destination_record = ctx.surface_registry->lookup(result->surface_handle());
        const auto source_record = ctx.surface_registry->lookup(inputs[0]->surface_handle());
        release_native_surface(ctx, *result);
        return NodeExecResult(NodeExecutionError{
            applied.error().code,
            std::string(name()),
            applied.error().message + "; framebuffer destination=" +
                std::to_string(result->width()) + "x" + std::to_string(result->height()) +
                (destination_record
                    ? "; registry destination=" + std::to_string(destination_record->desc.width) +
                        "x" + std::to_string(destination_record->desc.height)
                    : "; registry destination=missing") +
                "; framebuffer source=" + std::to_string(inputs[0]->width()) +
                "x" + std::to_string(inputs[0]->height()) +
                (source_record
                    ? "; registry source=" + std::to_string(source_record->desc.width) +
                        "x" + std::to_string(source_record->desc.height)
                    : "; registry source=missing")});
    }

    if (ctx.policy.require_native_gpu && !inputs[0]->data()) {
        const auto surface = inputs[0]->surface_handle();
        const bool native_surface_valid =
            ctx.backend && ctx.surface_registry &&
            surface != runtime::kInvalidRenderSurfaceHandle &&
            ctx.backend_surface_api()->is_native_surface_valid(surface);
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            std::string(name()),
            "lighting layer '" + m_layer_name + "' requires a valid native surface or CPU pixel backing; " +
                "surface_handle=" + std::to_string(surface) +
                " native_surface_valid=" + (native_surface_valid ? "true" : "false") +
                " gpu_authoritative=" + (inputs[0]->is_gpu_authoritative() ? "true" : "false")});
    }

    auto result = ctx.acquire_owned_fb(*inputs[0]);
    // Framebuffer pixels are canvas pixels; the pixel centre sits at +0.5 and the
    // layer depth is the translation z of its world matrix.
    for (i32 y = 0; y < result->height(); ++y) {
        Color* row = result->pixels_row(y);
        const f32 pixel_y = static_cast<f32>(result->origin_y() + y) + 0.5f;
        for (i32 x = 0; x < result->width(); ++x) {
            if (row[x].a <= 0.0f) continue;
            if (per_pixel_lights) {
                const Vec3 pixel{static_cast<f32>(result->origin_x() + x) + 0.5f, pixel_y, layer_z};
                row[x] = rendering::evaluate_lighting_at(
                    row[x], normal_world, pixel, light_context, m_material);
            } else {
                row[x] = rendering::evaluate_lighting(
                    row[x], normal_world, light_context, m_material);
            }
        }
    }
    // The CPU fallback rewrote the pixels in RAM. A cloned framebuffer can
    // still carry a GPU surface handle borrowed from its input, and the
    // inherited residency stays GPU-authoritative: every downstream consumer
    // (compositing, video output, raw readback) would then read the untouched
    // GPU image and silently drop this lighting pass. Publish the pixels that
    // were actually computed — detach the borrowed handle, which this node
    // does not own and must not release, and declare the CPU rows
    // authoritative.
    result->clear_surface_handle();
    result->mark_cpu_authoritative();
    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
