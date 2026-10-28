#include <chronon3d/render_graph/nodes/shadow_node.hpp>

#include "native_surface.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d::graph {
namespace {

// Vulkan's separable blur kernel accepts radii through 192 pixels. Depth-aware
// shadows can otherwise exceed that after projection expands world-space Z.
constexpr float kMaxNativeShadowBlurRadius = 192.0f;

} // namespace

NodeExecResult ShadowNode::execute_native(
    RenderGraphContext& ctx, const Framebuffer& source) const {
    OwnedFB promoted_source;
    OwnedFB output;
    std::vector<OwnedFB> scratch;

    const auto release_native = [&](OwnedFB& framebuffer) {
        if (framebuffer) release_native_surface(ctx, *framebuffer);
    };
    const auto cleanup = [&] {
        for (auto& framebuffer : scratch) release_native(framebuffer);
        release_native(promoted_source);
        release_native(output);
    };
    const auto fail = [&](RenderBackendErrorCode code, std::string message) {
        cleanup();
        return NodeExecResult{NodeExecutionError{
            code, "Shadow", std::move(message)}};
    };

    if (!ctx.backend || !ctx.surface_registry ||
        !ctx.backend_stats_api()->supports_native_surfaces()) {
        return fail(RenderBackendErrorCode::UnsupportedCapability,
                    "native shadow pass requires a native surface backend");
    }
    const int width = ctx.frame_input.width;
    const int height = ctx.frame_input.height;
    if (width <= 0 || height <= 0) {
        return fail(RenderBackendErrorCode::InvalidInput,
                    "native shadow pass requires a non-empty frame");
    }

    const Framebuffer* native_source = &source;
    const auto source_handle = source.surface_handle();
    const bool source_is_native =
        source_handle != runtime::kInvalidRenderSurfaceHandle &&
        ctx.surface_registry->lookup(source_handle).has_value() &&
        ctx.backend_surface_api()->is_native_surface_valid(source_handle);
    if (!source_is_native || source.is_cpu_authoritative()) {
        if (!source.data() || !source.is_cpu_authoritative()) {
            const bool source_registered =
                ctx.surface_registry->lookup(source_handle).has_value();
            const bool source_backend_valid =
                ctx.backend_surface_api()->is_native_surface_valid(source_handle);
            return fail(RenderBackendErrorCode::UnsupportedCapability,
                        "native shadow source has no current GPU surface or CPU pixels; "
                        "surface_handle=" + std::to_string(source_handle) +
                        " registered=" + (source_registered ? "true" : "false") +
                        " native_surface_valid=" + (source_backend_valid ? "true" : "false") +
                        " cpu_authoritative=" + (source.is_cpu_authoritative() ? "true" : "false"));
        }
        promoted_source = ctx.acquire_owned_fb(source);
        if (!promoted_source) {
            return fail(RenderBackendErrorCode::ExecutionFailure,
                        "failed to allocate native shadow source promotion");
        }
        // acquire_owned_fb copies the numeric handle but does not own it. Keep
        // the caller's binding untouched and upload the copied CPU pixels into
        // a new transient surface.
        promoted_source->clear_surface_handle();
        if (!ensure_native_surface(ctx, *promoted_source,
                                   "ShadowNode.native_source")) {
            return fail(RenderBackendErrorCode::ExecutionFailure,
                        "failed to promote the shadow caster to a GPU surface");
        }
        native_source = promoted_source.get();
    }

    output = ctx.acquire_owned_fb(width, height, false);
    if (!output || !ensure_empty_native_surface(ctx, *output)) {
        return fail(RenderBackendErrorCode::ExecutionFailure,
                    "failed to allocate native shadow output");
    }

    const auto allocate_scratch = [&]() -> Framebuffer* {
        auto framebuffer = ctx.acquire_owned_fb(width, height, false);
        if (!framebuffer || !ensure_empty_native_surface(ctx, *framebuffer)) {
            if (framebuffer) release_native_surface(ctx, *framebuffer);
            return nullptr;
        }
        scratch.push_back(std::move(framebuffer));
        return scratch.back().get();
    };
    Framebuffer* shifted = allocate_scratch();
    Framebuffer* tinted = allocate_scratch();
    Framebuffer* blur_scratch = allocate_scratch();
    Framebuffer* blurred = allocate_scratch();
    if (!shifted || !tinted || !blur_scratch || !blurred) {
        return fail(RenderBackendErrorCode::ExecutionFailure,
                    "failed to allocate native shadow scratch surfaces");
    }

    const float raw_dz = m_caster_world_z - m_receiver_world_z;
    const float dz = raw_dz == 0.0f ? m_depth_bias :
        std::copysign(std::abs(raw_dz) + m_depth_bias, raw_dz);
    const float depth = std::abs(dz);
    const float blur_factor = m_settings.depth_aware
        ? m_settings.blur_per_z : 0.0030f;
    const float opacity_falloff = m_settings.depth_aware
        ? m_settings.opacity_falloff : 0.0025f;
    const float blur_scale = 1.0f + depth * blur_factor;
    const float opacity_scale = 1.0f / (1.0f + depth * opacity_falloff);
    const float eps = 1e-4f;
    const float safe_y = std::abs(m_light_dir.y) > eps
        ? m_light_dir.y
        : std::copysign(eps, m_light_dir.y != 0.0f ? m_light_dir.y : 1.0f);
    float offset_x = -(m_light_dir.x / safe_y) * dz * m_settings.px_per_unit;
    float offset_y = (m_light_dir.z / safe_y) * dz * m_settings.px_per_unit;
    offset_x = std::clamp(offset_x, -m_settings.max_offset, m_settings.max_offset);
    offset_y = std::clamp(offset_y, -m_settings.max_offset, m_settings.max_offset);
    const int dx = static_cast<int>(std::round(offset_x));
    const int dy = static_cast<int>(std::round(offset_y));
    const float contact_blur = std::max(
        1.0f, std::max(m_settings.contact_blur_radius,
                       m_settings.blur_radius * 0.45f) * blur_scale);
    const float ambient_blur = std::max(
        contact_blur + 4.0f,
        std::max(m_settings.ambient_blur_radius,
                 m_settings.blur_radius * 2.0f) * blur_scale);
    const Color tint = m_settings.depth_aware
        ? m_settings.tint : Color{0.03f, 0.04f, 0.08f, 1.0f};

    NativeColorTransform colorize;
    colorize.mode = NativeColorTransform::Mode::ColorMatrix;
    colorize.p = {};
    // ColorMatrix consumes straight RGB plus alpha and premultiplies its
    // result. Map every covered caster pixel to the selected shadow tint.
    colorize.p[3][3] = 1.0f;
    colorize.p[4][0] = tint.r;
    colorize.p[4][1] = tint.g;
    colorize.p[4][2] = tint.b;

    const auto render_layer = [&](float layer_opacity, float blur_radius,
                                  float offset_scale) -> RenderOpResult {
        const int layer_dx = static_cast<int>(std::round(dx * offset_scale));
        const int layer_dy = static_cast<int>(std::round(dy * offset_scale));
        const float alpha = std::clamp(
            m_settings.opacity * layer_opacity * opacity_scale, 0.0f, 1.0f);
        auto shifted_result = ctx.backend_surface_api()->transform_surface(
            shifted->surface_handle(), native_source->surface_handle(),
            native_source->origin_x() + layer_dx,
            native_source->origin_y() + layer_dy, alpha);
        if (!shifted_result.ok()) return shifted_result;
        auto tint_result = ctx.backend_surface_api()->color_transform_surface(
            tinted->surface_handle(), shifted->surface_handle(), colorize);
        if (!tint_result.ok()) return tint_result;
        auto horizontal = ctx.backend_surface_api()->blur_surface(
            blur_scratch->surface_handle(), tinted->surface_handle(),
            std::clamp(blur_radius, 0.0f, kMaxNativeShadowBlurRadius), true);
        if (!horizontal.ok()) return horizontal;
        return ctx.backend_surface_api()->blur_surface(
            blurred->surface_handle(), blur_scratch->surface_handle(),
            std::clamp(blur_radius, 0.0f, kMaxNativeShadowBlurRadius), false);
    };

    const auto contact = render_layer(m_settings.contact_opacity,
                                      contact_blur, 1.0f);
    if (!contact.ok()) {
        return fail(contact.error().code, contact.error().message);
    }
    auto composed = ctx.backend_surface_api()->composite_surfaces(
        output->surface_handle(), blurred->surface_handle(),
        BlendMode::Normal, CompositeOperator::SourceOver);
    if (!composed.ok()) {
        return fail(composed.error().code, composed.error().message);
    }

    const auto ambient = render_layer(m_settings.ambient_opacity,
                                      ambient_blur, 1.75f);
    if (!ambient.ok()) {
        return fail(ambient.error().code, ambient.error().message);
    }
    composed = ctx.backend_surface_api()->composite_surfaces(
        output->surface_handle(), blurred->surface_handle(),
        BlendMode::Normal, CompositeOperator::SourceOver);
    if (!composed.ok()) {
        return fail(composed.error().code, composed.error().message);
    }

    for (auto& framebuffer : scratch) release_native(framebuffer);
    scratch.clear();
    release_native(promoted_source);
    output->set_origin(0, 0);
    output->set_opaque(false);
    output->mark_gpu_authoritative();
    return NodeExecResult{std::move(output)};
}

} // namespace chronon3d::graph
