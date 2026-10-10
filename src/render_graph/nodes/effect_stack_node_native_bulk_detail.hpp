// Included inside namespace chronon3d::graph by effect_stack_node.cpp.
//
// Native bulk routes for TICKET-VULKAN-EFFECT-PARITY wave 2 (22 effects).
// Four shared dispatches cover the remaining catalog:
//   radial_blur_surface — RadialBlur (zoom+spin taps, centroid-stable window)
//   warp_bulk_surface   — Fake3DWave, CornerPin, Twirl, Bulge, MeshWarp(<=3x3),
//                         TurbulentDisplace, WaveWarp, Ripple, DisplacementMap
//                         (map-image variant), LensDistortion, FoldWarp,
//                         RoughenEdges, Stroke
//   generate_bulk_surface — FractalNoise (procedural), FourColorGradient,
//                         LightRays, Halftone, LuminanceResponse (no-blur),
//                         CreaseShading
//   lut_bulk_surface    — Curves (host-baked 256 tables), Lut3D (dim<=16)
// Every helper follows the directional-blur protocol: full frame only
// (partial clips fail closed), single-effect stacks, finite certified
// parameter windows, identity fast paths forwarded without dispatch, and
// fail-closed (false) outside the window so strict policy never silently
// falls back to software.
namespace native_effects {

namespace bulk_detail {

inline bool bulk_finite_list(std::initializer_list<float> vs) {
    for (float v : vs)
        if (!std::isfinite(v)) return false;
    return true;
}

inline int bulk_edge_to_int(sampling::EdgeMode mode, bool& ok) {
    if (mode == sampling::EdgeMode::Transparent) { ok = true; return 0; }
    if (mode == sampling::EdgeMode::Clamp) { ok = true; return 1; }
    ok = false;
    return 0;
}

// Common single-dispatch prologue: borrow or reuse the source surface and
// allocate a same-size destination. Returns invalid handle on failure.
inline runtime::RenderSurfaceHandle bulk_alloc_destination(RenderGraphContext& ctx,
                                                     Framebuffer& result,
                                                     runtime::RenderSurfaceHandle borrowed_source,
                                                     runtime::RenderSurfaceHandle& source_out) {
    const bool has_borrowed = borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool has_result_surface =
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend->is_native_surface_valid(result.surface_handle());
    if (!has_borrowed && !has_result_surface) return runtime::kInvalidRenderSurfaceHandle;
    source_out = has_borrowed ? borrowed_source : result.surface_handle();
    const auto desc = native_surface_desc(result.width(), result.height());
    const auto destination = ctx.surface_registry->create(desc);
    if (destination == runtime::kInvalidRenderSurfaceHandle) return destination;
    if (!ctx.backend->create_surface(destination, desc).ok()) {
        ctx.surface_registry->release(destination);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    return destination;
}

inline void bulk_release_surface(RenderGraphContext& ctx,
                            runtime::RenderSurfaceHandle h) {
    (void)ctx.backend->release_surface(h);
    ctx.surface_registry->release(h);
}

// Forward helper for CPU identity fast paths (mirrors the directional-blur
// convention: attach the borrowed surface when present).
inline bool bulk_true_forward(RenderGraphContext& ctx, Framebuffer& result,
                         runtime::RenderSurfaceHandle borrowed_source) {
    if (borrowed_source != runtime::kInvalidRenderSurfaceHandle)
        result.set_surface_handle(borrowed_source);
    (void)ctx;
    return true;
}

// Upload a raw RGBA32F span as an aux surface (Curves tables, Lut3D table).
inline runtime::RenderSurfaceHandle bulk_upload_aux(RenderGraphContext& ctx,
                                               int w, int h,
                                               std::span<const float> rgba) {
    const auto desc = native_surface_desc(w, h);
    const auto aux = ctx.surface_registry->create(desc);
    if (aux == runtime::kInvalidRenderSurfaceHandle) return aux;
    if (!ctx.backend->create_surface(aux, desc).ok()) {
        ctx.surface_registry->release(aux);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    if (!ctx.backend->upload_surface(aux, desc, rgba).ok()) {
        bulk_release_surface(ctx, aux);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    return aux;
}

} // namespace bulk_detail

// ── RadialBlur ─────────────────────────────────────────────────────────────
bool try_native_full_frame_radial_blur(RenderGraphContext& ctx,
                                       const EffectStack& effect_stack,
                                       Framebuffer& result,
                                       const std::optional<raster::BBox>& clip,
                                       runtime::RenderSurfaceHandle borrowed_source) {
    using namespace bulk_detail;
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::RadialBlur) return false;
    const auto* p = std::get_if<RadialBlurParams>(&instance.params);
    if (!p) return false;
    if (!std::isfinite(p->center.x) || !std::isfinite(p->center.y) ||
        !std::isfinite(p->amount)) {
        return false;
    }
    // CPU identity fast paths: amount <= 0 or samples <= 1 forward untouched.
    if (!(p->amount > 0.0f) || p->render_samples <= 1)
        return bulk_detail::bulk_true_forward(ctx, result, borrowed_source);
    if (p->amount > 8.0f) return false;
    const int taps = p->render_samples;
    if (taps < 2 || taps > 64) return false;
    runtime::RenderSurfaceHandle source = runtime::kInvalidRenderSurfaceHandle;
    const auto destination = bulk_alloc_destination(ctx, result, borrowed_source, source);
    if (destination == runtime::kInvalidRenderSurfaceHandle) {
        // Identity-forward case already returned; reaching here with no
        // surface means failure, except the forward path handled above.
        return false;
    }
    const float cx_px =
        p->center.x * static_cast<float>(std::max(0, result.width() - 1)) + 0.5f;
    const float cy_px =
        p->center.y * static_cast<float>(std::max(0, result.height() - 1)) + 0.5f;
    const auto dispatched = ctx.backend->radial_blur_surface(
        destination, source, cx_px, cy_px, p->amount, taps);
    if (!dispatched.ok()) {
        bulk_release_surface(ctx, destination);
        return false;
    }
    result.set_surface_handle(destination);
    return true;
}

// ── WarpBulk single-effect dispatcher ──────────────────────────────────────
bool try_native_full_frame_warp_bulk(RenderGraphContext& ctx,
                                     const EffectStack& effect_stack,
                                     Framebuffer& result,
                                     const std::optional<raster::BBox>& clip,
                                     runtime::RenderSurfaceHandle borrowed_source,
                                     runtime::RenderSurfaceHandle map_source =
                                         runtime::kInvalidRenderSurfaceHandle,
                                     const Framebuffer* map_framebuffer = nullptr,
                                     const Framebuffer* src_framebuffer = nullptr) {
    using namespace bulk_detail;
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled) return false;
    const float time = ctx.frame_input.time_seconds;
    if (!std::isfinite(time)) return false;
    const float w = static_cast<float>(result.width());
    const float h = static_cast<float>(result.height());
    const float mindim = static_cast<float>(std::min(result.width(), result.height()));
    WarpBulkParams bulk;
    bool edge_ok = false;

    switch (instance.effect_type) {
    case effects::EffectType::CornerPin: {
        const auto* p = std::get_if<CornerPinParams>(&instance.params);
        if (!p) return false;
        for (Vec2 c : {p->top_left, p->top_right, p->bottom_right, p->bottom_left}) {
            if (!std::isfinite(c.x) || !std::isfinite(c.y) ||
                std::abs(c.x) > 4.0f || std::abs(c.y) > 4.0f)
                return false;
        }
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 0;
        bulk.p[0] = p->top_left.x * w; bulk.p[1] = p->top_left.y * h;
        bulk.p[2] = p->top_right.x * w; bulk.p[3] = p->top_right.y * h;
        bulk.p[4] = p->bottom_right.x * w; bulk.p[5] = p->bottom_right.y * h;
        bulk.p[6] = p->bottom_left.x * w; bulk.p[7] = p->bottom_left.y * h;
        break;
    }
    case effects::EffectType::Twirl: {
        const auto* p = std::get_if<TwirlParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->center.x, p->center.y, p->angle_degrees, p->radius, p->falloff}) ||
            p->radius <= 0.0f || p->radius > 2.0f || p->falloff <= 0.0f ||
            p->falloff > 4.0f || std::abs(p->angle_degrees) > 3600.0f)
            return false;
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 1;
        bulk.p[0] = p->center.x * w; bulk.p[1] = p->center.y * h;
        bulk.p[2] = p->radius * mindim;
        bulk.p[3] = p->angle_degrees * 0.01745329251994329577f;
        bulk.p[4] = p->falloff;
        break;
    }
    case effects::EffectType::Bulge: {
        const auto* p = std::get_if<BulgeParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->center.x, p->center.y, p->amount, p->radius}) ||
            p->amount < -1.0f || p->amount > 1.0f || p->radius <= 0.0f ||
            p->radius > 2.0f)
            return false;
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 2;
        bulk.p[0] = p->center.x * w; bulk.p[1] = p->center.y * h;
        bulk.p[2] = p->radius * mindim; bulk.p[3] = p->amount;
        break;
    }
    case effects::EffectType::MeshWarp: {
        const auto* p = std::get_if<MeshWarpParams>(&instance.params);
        if (!p) return false;
        // Certified window: grids up to 3x3 (18 floats fit the push block).
        if (p->cols < 2 || p->cols > 3 || p->rows < 2 || p->rows > 3 ||
            !std::isfinite(p->smoothness) || p->smoothness < 0.0f ||
            p->smoothness > 1.0f ||
            p->offsets.size() != static_cast<std::size_t>(p->cols * p->rows))
            return false;
        for (Vec2 o : p->offsets) {
            if (!std::isfinite(o.x) || !std::isfinite(o.y) ||
                std::abs(o.x) > 4.0f || std::abs(o.y) > 4.0f)
                return false;
        }
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 3;
        bulk.p[0] = static_cast<float>(p->cols);
        bulk.p[1] = static_cast<float>(p->rows);
        bulk.p[2] = p->smoothness;
        for (int i = 0; i < 9; ++i) {
            Vec2 o{0.0f, 0.0f};
            if (i < static_cast<int>(p->offsets.size())) o = p->offsets[static_cast<std::size_t>(i)];
            bulk.p[3 + i * 2] = o.x;
            bulk.p[3 + i * 2 + 1] = o.y;
        }
        break;
    }
    case effects::EffectType::TurbulentDisplace: {
        const auto* p = std::get_if<TurbulentDisplaceParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->amount, p->size, p->evolution, p->evolution_speed}) ||
            p->amount < 0.0f || p->amount > 1.0f || p->size <= 0.0f ||
            p->complexity < 1 || p->complexity > 16)
            return false;
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 4;
        bulk.p[0] = p->amount; bulk.p[1] = p->size;
        bulk.p[2] = p->evolution + p->evolution_speed * time;
        bulk.p[3] = static_cast<float>(p->complexity);
        break;
    }
    case effects::EffectType::WaveWarp: {
        const auto* p = std::get_if<WaveWarpParams>(&instance.params);
        if (!p) return false;
        const float len = std::sqrt(p->direction.x * p->direction.x +
                                    p->direction.y * p->direction.y);
        if (!std::isfinite(len) || len < 1.0e-6f ||
            !bulk_finite_list({p->amplitude, p->wavelength, p->speed, p->phase}) ||
            p->wavelength <= 0.0f)
            return false;
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 5;
        bulk.p[0] = static_cast<float>(static_cast<std::uint8_t>(p->type));
        bulk.p[1] = p->direction.x / len; bulk.p[2] = p->direction.y / len;
        bulk.p[3] = p->amplitude; bulk.p[4] = p->wavelength;
        bulk.p[5] = p->phase + 6.2831853071795864769f * p->speed * time;
        break;
    }
    case effects::EffectType::Ripple: {
        const auto* p = std::get_if<RippleParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->center.x, p->center.y, p->radius, p->wave_speed,
                          p->width, p->wave_height, p->damping}) ||
            p->radius <= 0.0f || p->width <= 0.0f || p->damping < 0.0f)
            return false;
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 6;
        bulk.p[0] = p->center.x * w; bulk.p[1] = p->center.y * h;
        bulk.p[2] = p->radius; bulk.p[3] = p->wave_speed;
        bulk.p[4] = p->width; bulk.p[5] = p->wave_height;
        bulk.p[6] = p->damping; bulk.p[7] = time;
        break;
    }
    case effects::EffectType::LensDistortion: {
        const auto* p = std::get_if<LensDistortionParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->curvature, p->center.x, p->center.y, p->scale}) ||
            p->curvature < -1.0f || p->curvature > 1.0f || p->scale < 0.1f ||
            p->scale > 4.0f)
            return false;
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 7;
        bulk.p[0] = p->curvature;
        bulk.p[1] = p->center.x * w; bulk.p[2] = p->center.y * h;
        bulk.p[3] = std::clamp(p->scale, 0.1f, 4.0f);
        break;
    }
    case effects::EffectType::FoldWarp: {
        const auto* p = std::get_if<FoldWarpParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->position, p->amount, p->radius, p->falloff,
                          p->perspective}) ||
            p->position < 0.0f || p->position > 1.0f || p->amount < -1.0f ||
            p->amount > 1.0f || p->radius < 0.01f || p->radius > 1.0f ||
            p->falloff < 0.1f || p->falloff > 4.0f || p->perspective < 0.0f ||
            p->perspective > 0.5f)
            return false;
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 8;
        bulk.p[0] = (p->axis == FoldAxis::Horizontal) ? 1.0f : 0.0f;
        bulk.p[1] = p->position; bulk.p[2] = p->amount;
        bulk.p[3] = p->radius; bulk.p[4] = p->falloff;
        bulk.p[5] = p->perspective;
        break;
    }
    case effects::EffectType::Fake3DWave: {
        const auto* p = std::get_if<Fake3DWaveParams>(&instance.params);
        if (!p) return false;
        // CPU identity fast path first (mirrors apply_fake_3d_wave).
        if (!p->shadow_enabled && p->amplitude_px <= 0.0f && p->depth_px <= 0.0f)
            return bulk_true_forward(ctx, result, borrowed_source);
        // Certified window: body pass only (shadow comps fail closed).
        if (p->shadow_enabled) return false;
        if (!bulk_finite_list({p->amplitude_px, p->frequency, p->speed, p->depth_px,
                          p->phase, p->perspective, p->highlight,
                          p->side_darkening}) ||
            p->slices < 1 || p->slices > 256)
            return false;
        // Body-pass sampler on the CPU is Transparent-edged:
        // Framebuffer::sample_bilinear fetches corners via get_pixel, which
        // returns transparent outside the canvas.
        bulk.edge_mode = 0;
        bulk.mode = 9;
        bulk.p[0] = p->amplitude_px; bulk.p[1] = p->frequency;
        bulk.p[2] = p->speed; bulk.p[3] = p->depth_px;
        bulk.p[4] = p->phase;
        bulk.p[5] = static_cast<float>(std::clamp(p->slices, 1, 256));
        bulk.p[6] = (p->axis == WaveAxis::Vertical) ? 1.0f : 0.0f;
        bulk.p[7] = p->perspective;
        bulk.p[8] = p->highlight; bulk.p[9] = p->side_darkening;
        bulk.p[10] = time;
        break;
    }
    case effects::EffectType::RoughenEdges: {
        const auto* p = std::get_if<RoughenEdgesParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->amount, p->scale, p->evolution,
                          p->evolution_speed}) ||
            p->amount < 0.0f || p->amount > 64.0f || p->scale <= 0.0f ||
            p->scale > 1024.0f || p->complexity < 1 || p->complexity > 4)
            return false;
        if (p->amount == 0.0f) return bulk_true_forward(ctx, result, borrowed_source);
        bulk.edge_mode = 0;  // boundary test uses clamped alpha fetches internally
        bulk.mode = 10;
        bulk.p[0] = p->amount; bulk.p[1] = p->scale;
        bulk.p[2] = static_cast<float>(p->complexity);
        bulk.p[3] = p->evolution + p->evolution_speed * time;
        bulk.p[4] = static_cast<float>(p->seed);
        break;
    }
    case effects::EffectType::Stroke: {
        const auto* p = std::get_if<StrokeParams>(&instance.params);
        if (!p) return false;
        if (p->width <= 0.0f) return bulk_true_forward(ctx, result, borrowed_source);
        if (!bulk_finite_list({p->color.r, p->color.g, p->color.b, p->color.a,
                          p->width, p->softness}))
            return false;
        int outer_radius = 0, inner_radius = 0;
        int smode = 0;
        switch (p->mode) {
        case StrokeMode::Outside:
            outer_radius = static_cast<int>(std::ceil(p->width + p->softness));
            smode = 0;
            break;
        case StrokeMode::Inside:
            inner_radius = static_cast<int>(std::ceil(p->width + p->softness));
            smode = 1;
            break;
        case StrokeMode::Center:
            outer_radius = static_cast<int>(std::ceil(p->width * 0.5f + p->softness));
            inner_radius = outer_radius;
            smode = 2;
            break;
        }
        // Certified morphology radius (box loop cost); larger strokes fail closed.
        if (outer_radius > 16 || inner_radius > 16) return false;
        bulk.edge_mode = 0;
        bulk.mode = 11;
        bulk.p[0] = p->color.r; bulk.p[1] = p->color.g; bulk.p[2] = p->color.b;
        bulk.p[3] = static_cast<float>(smode);
        bulk.p[4] = static_cast<float>(outer_radius);
        bulk.p[5] = static_cast<float>(inner_radius);
        break;
    }
    case effects::EffectType::DisplacementMap: {
        const auto* p = std::get_if<DisplacementMapParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->intensity_x, p->intensity_y}) ||
            std::abs(p->intensity_x) > 4.0f || std::abs(p->intensity_y) > 4.0f)
            return false;
        // Certified window: map-image variant with a resolved second input.
        // Procedural-field maps fail closed (Field2D port is future work).
        // An empty map_layer throws on the CPU oracle, so it fails closed
        // here too.
        if (p->map_layer.empty()) return false;
        if (p->field.has_value()) return false;
        if (map_source == runtime::kInvalidRenderSurfaceHandle ||
            map_framebuffer == nullptr || src_framebuffer == nullptr)
            return false;
        bulk.edge_mode = bulk_edge_to_int(p->edge_mode, edge_ok);
        if (!edge_ok) return false;
        bulk.mode = 12;
        bulk.p[0] = p->intensity_x; bulk.p[1] = p->intensity_y;
        bulk.p[2] = static_cast<float>(static_cast<std::uint8_t>(p->channel));
        bulk.p[3] = static_cast<float>(src_framebuffer->origin_x() -
                                       map_framebuffer->origin_x());
        bulk.p[4] = static_cast<float>(src_framebuffer->origin_y() -
                                       map_framebuffer->origin_y());
        break;
    }
    default:
        return false;
    }

    runtime::RenderSurfaceHandle source = runtime::kInvalidRenderSurfaceHandle;
    const auto destination = bulk_alloc_destination(ctx, result, borrowed_source, source);
    if (destination == runtime::kInvalidRenderSurfaceHandle) return false;
    const auto aux = (bulk.mode == 12) ? map_source : source;
    const auto dispatched = ctx.backend->warp_bulk_surface(destination, source, aux, bulk);
    if (!dispatched.ok()) {
        bulk_release_surface(ctx, destination);
        return false;
    }
    result.set_surface_handle(destination);
    return true;
}

// ── GenerateBulk single-effect dispatcher ──────────────────────────────────
bool try_native_full_frame_generate_bulk(RenderGraphContext& ctx,
                                         const EffectStack& effect_stack,
                                         Framebuffer& result,
                                         const std::optional<raster::BBox>& clip,
                                         runtime::RenderSurfaceHandle borrowed_source) {
    using namespace bulk_detail;
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled) return false;
    const float time = ctx.frame_input.time_seconds;
    if (!std::isfinite(time)) return false;
    GenerateBulkParams bulk;

    switch (instance.effect_type) {
    case effects::EffectType::FractalNoise: {
        const auto* p = std::get_if<FractalNoiseParams>(&instance.params);
        if (!p) return false;
        if (p->field.has_value()) return false;  // Field2D port is future work
        if (!bulk_finite_list({p->frequency, p->amplitude, p->evolution,
                          p->contrast, p->brightness}))
            return false;
        bulk.mode = 0;
        bulk.flags = (p->color_mode == NoiseColorMode::RGB) ? 1 : 0;
        const std::uint32_t oct = std::clamp(p->octaves, 1u, 16u);
        bulk.a[0] = static_cast<float>(p->seed);
        bulk.a[1] = static_cast<float>(oct);
        bulk.a[2] = p->frequency; bulk.a[3] = p->amplitude;
        bulk.a[4] = p->evolution * time;
        bulk.a[5] = p->contrast; bulk.a[6] = p->brightness;
        break;
    }
    case effects::EffectType::FourColorGradient: {
        const auto* p = std::get_if<FourColorGradientParams>(&instance.params);
        if (!p) return false;
        for (const Color& c : p->colors) {
            if (!bulk_finite_list({c.r, c.g, c.b, c.a})) return false;
        }
        for (Vec2 pt : p->points) {
            if (!std::isfinite(pt.x) || !std::isfinite(pt.y)) return false;
        }
        const float smooth = std::isfinite(p->blend_smoothness)
            ? std::max(p->blend_smoothness, 0.0f)
            : 0.0f;
        bulk.mode = 1;
        bulk.a[0] = p->colors[0].r; bulk.a[1] = p->colors[0].g;
        bulk.a[2] = p->colors[0].b; bulk.a[3] = p->colors[0].a;
        bulk.a[4] = p->colors[1].r; bulk.a[5] = p->colors[1].g;
        bulk.a[6] = p->colors[1].b; bulk.a[7] = p->colors[1].a;
        bulk.a[8] = p->colors[2].r; bulk.a[9] = p->colors[2].g;
        bulk.a[10] = p->colors[2].b; bulk.a[11] = p->colors[2].a;
        bulk.a[12] = p->colors[3].r; bulk.a[13] = p->colors[3].g;
        bulk.a[14] = p->colors[3].b; bulk.a[15] = p->colors[3].a;
        bulk.b[0] = p->points[0].x; bulk.b[1] = p->points[0].y;
        bulk.b[2] = p->points[1].x; bulk.b[3] = p->points[1].y;
        bulk.b[4] = p->points[2].x; bulk.b[5] = p->points[2].y;
        bulk.b[6] = p->points[3].x; bulk.b[7] = p->points[3].y;
        bulk.b[8] = smooth;
        break;
    }
    case effects::EffectType::LightRays: {
        const auto* p = std::get_if<LightRaysParams>(&instance.params);
        if (!p) return false;
        const float max_length =
            std::isfinite(p->length) ? std::max(p->length, 0.0f) : 0.0f;
        const float density = std::isfinite(p->density)
            ? std::clamp(p->density, 0.0f, 8.0f)
            : 0.0f;
        if (max_length <= 0.0f || density <= 0.0f)
            return bulk_true_forward(ctx, result, borrowed_source);
        if (!bulk_finite_list({p->decay, p->color.r, p->color.g, p->color.b,
                          p->color.a}))
            return false;
        bulk.mode = 2;
        bulk.a[0] = std::isfinite(p->origin.x) ? p->origin.x : 0.5f;
        bulk.a[1] = std::isfinite(p->origin.y) ? p->origin.y : 0.5f;
        // NOTE: origin is normalized on the CPU; convert to pixels here.
        bulk.a[0] *= static_cast<float>(result.width());
        bulk.a[1] *= static_cast<float>(result.height());
        // Packing mirrors the shader header: a0=(origin_px), a1=(length,
        // density, decay), a2=(color).
        bulk.a[4] = max_length; bulk.a[5] = density;
        bulk.a[6] = std::isfinite(p->decay)
            ? std::clamp(p->decay, 0.0f, 1.0f)
            : 0.0f;
        bulk.a[8] = p->color.r; bulk.a[9] = p->color.g;
        bulk.a[10] = p->color.b; bulk.a[11] = p->color.a;
        break;
    }
    case effects::EffectType::Halftone: {
        const auto* p = std::get_if<HalftoneParams>(&instance.params);
        if (!p) return false;
        if (p->size == 0 || p->size > 128 || !std::isfinite(p->angle_degrees) ||
            !std::isfinite(p->amount) || p->amount < 0.0f || p->amount > 1.0f)
            return false;
        bulk.mode = 3;
        const float rad = p->angle_degrees * 0.01745329251994329577f;
        bulk.a[0] = static_cast<float>(p->size);
        bulk.a[1] = std::cos(rad); bulk.a[2] = std::sin(rad);
        bulk.a[4] = p->amount;
        break;
    }
    case effects::EffectType::LuminanceResponse: {
        const auto* p = std::get_if<LuminanceResponseParams>(&instance.params);
        if (!p) return false;
        try {
            p->validate();
        } catch (...) {
            return false;
        }
        // Certified window: no-blur path (radius <= 0.5 skips the CPU blur).
        if (p->radius > 0.5f) return false;
        bulk.mode = 4;
        bulk.a[0] = p->threshold;
        bulk.a[1] = p->softness;
        bulk.a[2] = p->intensity;
        bulk.a[3] = (p->blend == LuminanceBlendMode::Add)
            ? 0.0f
            : ((p->blend == LuminanceBlendMode::Screen) ? 1.0f : 2.0f);
        bulk.a[4] = p->tint.r; bulk.a[5] = p->tint.g; bulk.a[6] = p->tint.b;
        break;
    }
    case effects::EffectType::CreaseShading: {
        const auto* p = std::get_if<CreaseShadingParams>(&instance.params);
        if (!p) return false;
        if (!bulk_finite_list({p->position, p->width, p->dark_side, p->light_side,
                          p->softness}) ||
            p->position < 0.0f || p->position > 1.0f || p->width < 1.0f ||
            p->width > 256.0f || p->dark_side < 0.0f || p->dark_side > 1.0f ||
            p->light_side < 0.0f || p->light_side > 1.0f || p->softness < 0.0f ||
            p->softness > 1.0f)
            return false;
        bulk.mode = 5;
        bulk.a[0] = (p->axis == FoldAxis::Horizontal) ? 1.0f : 0.0f;
        bulk.a[1] = p->position *
            ((p->axis == FoldAxis::Horizontal)
                 ? static_cast<float>(result.height())
                 : static_cast<float>(result.width()));
        bulk.a[2] = std::max(p->width, 1.0f);
        bulk.a[4] = p->dark_side; bulk.a[5] = p->light_side;
        bulk.a[6] = p->softness;
        break;
    }
    default:
        return false;
    }

    runtime::RenderSurfaceHandle source = runtime::kInvalidRenderSurfaceHandle;
    const auto destination = bulk_alloc_destination(ctx, result, borrowed_source, source);
    if (destination == runtime::kInvalidRenderSurfaceHandle) return false;
    const auto dispatched = ctx.backend->generate_bulk_surface(destination, source, bulk);
    if (!dispatched.ok()) {
        bulk_release_surface(ctx, destination);
        return false;
    }
    result.set_surface_handle(destination);
    return true;
}

// ── LutBulk single-effect dispatcher ───────────────────────────────────────
bool try_native_full_frame_lut_bulk(RenderGraphContext& ctx,
                                    const EffectStack& effect_stack,
                                    Framebuffer& result,
                                    const std::optional<raster::BBox>& clip,
                                    runtime::RenderSurfaceHandle borrowed_source) {
    using namespace bulk_detail;
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled) return false;
    LutBulkParams bulk;

    if (instance.effect_type == effects::EffectType::Curves) {
        const auto* p = std::get_if<CurvesParams>(&instance.params);
        if (!p) return false;
        if (p->master.empty() && p->red.empty() && p->green.empty() &&
            p->blue.empty())
            return bulk_true_forward(ctx, result, borrowed_source);
        // Bake host CompiledCurve tables (256 entries each; absent = identity).
        CompiledCurve master_curve(p->master);
        CompiledCurve red_curve(p->red);
        CompiledCurve green_curve(p->green);
        CompiledCurve blue_curve(p->blue);
        std::vector<float> table(256 * 4 * 4, 0.0f);
        for (int i = 0; i < 256; ++i) {
            const float x = static_cast<float>(i) / 255.0f;
            table[(0 * 256 + i) * 4] = p->master.empty() ? x : master_curve.lut_entry(static_cast<std::size_t>(i));
            table[(1 * 256 + i) * 4] = p->red.empty() ? x : red_curve.lut_entry(static_cast<std::size_t>(i));
            table[(2 * 256 + i) * 4] = p->green.empty() ? x : green_curve.lut_entry(static_cast<std::size_t>(i));
            table[(3 * 256 + i) * 4] = p->blue.empty() ? x : blue_curve.lut_entry(static_cast<std::size_t>(i));
        }
        bulk.mode = 0;
        int flags = 0;
        if (!p->master.empty()) flags |= 1;
        if (!p->red.empty()) flags |= 2;
        if (!p->green.empty()) flags |= 4;
        if (!p->blue.empty()) flags |= 8;
        if (!p->master.empty() && master_curve.is_identity()) flags |= 16;
        if (!p->red.empty() && red_curve.is_identity()) flags |= 32;
        if (!p->green.empty() && green_curve.is_identity()) flags |= 64;
        if (!p->blue.empty() && blue_curve.is_identity()) flags |= 128;
        bulk.meta[1] = flags;
        runtime::RenderSurfaceHandle source = runtime::kInvalidRenderSurfaceHandle;
        const auto destination = bulk_alloc_destination(ctx, result, borrowed_source, source);
        if (destination == runtime::kInvalidRenderSurfaceHandle) return false;
        const auto aux = bulk_upload_aux(ctx, 256, 4, table);
        if (aux == runtime::kInvalidRenderSurfaceHandle) {
            bulk_release_surface(ctx, destination);
            return false;
        }
        const auto dispatched = ctx.backend->lut_bulk_surface(destination, source, aux, bulk);
        bulk_release_surface(ctx, aux);
        if (!dispatched.ok()) {
            bulk_release_surface(ctx, destination);
            return false;
        }
        result.set_surface_handle(destination);
        return true;
    }

    if (instance.effect_type == effects::EffectType::Lut3D) {
        const auto* p = std::get_if<Lut3DParams>(&instance.params);
        if (!p) return false;
        const std::size_t n = p->dimension;
        const std::size_t expected = n * n * n;
        // Mirror the CPU processor contract exactly: negative/NaN/oversized
        // amounts throw there, so they fail closed here; amount == 0 is the
        // CPU no-op and forwards the input untouched.
        if (n < 2 || n > 16 || !p->values || p->values->size() != expected ||
            !std::isfinite(p->amount) || p->amount < 0.0f || p->amount > 1.0f)
            return false;
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(p->domain_min[static_cast<std::size_t>(c)]) ||
                !std::isfinite(p->domain_max[static_cast<std::size_t>(c)]) ||
                !(p->domain_max[static_cast<std::size_t>(c)] >
                  p->domain_min[static_cast<std::size_t>(c)]))
                return false;
        }
        if (p->amount == 0.0f) return bulk_true_forward(ctx, result, borrowed_source);
        // Flatten red-fastest table to a dim x (dim*dim) RGBA image.
        const int dim = static_cast<int>(n);
        std::vector<float> table(static_cast<std::size_t>(dim * dim * dim) * 4, 0.0f);
        for (int iz = 0; iz < dim; ++iz)
            for (int iy = 0; iy < dim; ++iy)
                for (int ix = 0; ix < dim; ++ix) {
                    const auto& e = (*p->values)[static_cast<std::size_t>(iz * dim * dim + iy * dim + ix)];
                    const std::size_t t =
                        (static_cast<std::size_t>(iz * dim + iy) * static_cast<std::size_t>(dim) +
                         static_cast<std::size_t>(ix)) * 4;
                    table[t] = e[0]; table[t + 1] = e[1]; table[t + 2] = e[2];
                    table[t + 3] = 1.0f;
                }
        bulk.mode = 1;
        bulk.amount = p->amount;
        for (int c = 0; c < 3; ++c) {
            bulk.dmin[c] = p->domain_min[static_cast<std::size_t>(c)];
            bulk.dmax[c] = p->domain_max[static_cast<std::size_t>(c)];
        }
        bulk.meta[0] = dim;
        runtime::RenderSurfaceHandle source = runtime::kInvalidRenderSurfaceHandle;
        const auto destination = bulk_alloc_destination(ctx, result, borrowed_source, source);
        if (destination == runtime::kInvalidRenderSurfaceHandle) return false;
        const auto aux = bulk_upload_aux(ctx, dim, dim * dim, table);
        if (aux == runtime::kInvalidRenderSurfaceHandle) {
            bulk_release_surface(ctx, destination);
            return false;
        }
        const auto dispatched = ctx.backend->lut_bulk_surface(destination, source, aux, bulk);
        bulk_release_surface(ctx, aux);
        if (!dispatched.ok()) {
            bulk_release_surface(ctx, destination);
            return false;
        }
        result.set_surface_handle(destination);
        return true;
    }

    return false;
}

} // namespace native_effects
