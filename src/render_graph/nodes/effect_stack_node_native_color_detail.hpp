// Included inside namespace chronon3d::graph by effect_stack_node.cpp.
//
// Native per-pixel color effects (TICKET-VULKAN-EFFECT-PARITY wave 1).  Each
// effect is lowered to a NativeColorTransform that reproduces the scalar
// software processor exactly in structure (same alpha<=0 skip, same clamp,
// same premultiplied/straight domain, same identity early-outs).  Any
// parameter set that cannot be lowered returns nullopt and the caller fails
// closed under strict native-GPU policy.
namespace native_effects {

struct NativeColorLowering {
    NativeColorTransform transform{};
    bool honors_clip{false};  ///< software processor restricts to context.clip
};

namespace color_detail {

inline bool finite_all(std::initializer_list<float> values) {
    for (const float v : values)
        if (!std::isfinite(v)) return false;
    return true;
}

inline NativeColorTransform identity_affine() { return NativeColorTransform{}; }

inline NativeColorTransform scaled_affine(float scale, float offset, bool clamp) {
    NativeColorTransform t;
    t.p[0] = {scale, 0.0f, 0.0f, offset};
    t.p[1] = {0.0f, scale, 0.0f, offset};
    t.p[2] = {0.0f, 0.0f, scale, offset};
    t.clamp_unit = clamp;
    return t;
}

} // namespace color_detail

inline std::optional<NativeColorLowering>
lower_native_color_effect(const effects::EffectInstance& instance,
                          int origin_x = 0, int origin_y = 0) {
    using color_detail::finite_all;
    using color_detail::identity_affine;
    using color_detail::scaled_affine;
    NativeColorLowering out;
    switch (instance.effect_type) {
    case effects::EffectType::Brightness: {
        const auto* p = std::get_if<BrightnessParams>(&instance.params);
        if (!p || !finite_all({p->value})) return std::nullopt;
        // effect_color.cpp: clamp((v + b - 0.5) * 1 + 0.5), skipped when b == 0.
        out.transform = p->value == 0.0f ? identity_affine()
                                         : scaled_affine(1.0f, p->value, true);
        out.honors_clip = true;
        return out;
    }
    case effects::EffectType::Contrast: {
        const auto* p = std::get_if<ContrastParams>(&instance.params);
        if (!p || !finite_all({p->value})) return std::nullopt;
        // clamp((v - 0.5) * c + 0.5), skipped when c == 1.
        out.transform = p->value == 1.0f
            ? identity_affine()
            : scaled_affine(p->value, 0.5f - 0.5f * p->value, true);
        out.honors_clip = true;
        return out;
    }
    case effects::EffectType::Saturation: {
        const auto* p = std::get_if<SaturationParams>(&instance.params);
        if (!p || !finite_all({p->value})) return std::nullopt;
        out.honors_clip = true;
        if (p->value == 1.0f) { out.transform = identity_affine(); return out; }
        // lum + s * (c - lum) == s * c + (1 - s) * lum, then clamp.
        const float s = p->value;
        const float k = 1.0f - s;
        const float lr = 0.2126f * k, lg = 0.7152f * k, lb = 0.0722f * k;
        out.transform.p[0] = {lr + s, lg, lb, 0.0f};
        out.transform.p[1] = {lr, lg + s, lb, 0.0f};
        out.transform.p[2] = {lr, lg, lb + s, 0.0f};
        out.transform.clamp_unit = true;
        return out;
    }
    case effects::EffectType::HueRotate: {
        const auto* p = std::get_if<HueRotateParams>(&instance.params);
        if (!p || !finite_all({p->degrees})) return std::nullopt;
        out.honors_clip = true;
        if (p->degrees == 0.0f) { out.transform = identity_affine(); return out; }
        // Same float coefficients as apply_hue_rotate().
        const float rad = p->degrees * (3.14159265f / 180.0f);
        const float cos_a = std::cos(rad);
        const float sin_a = std::sin(rad);
        const float s3 = 1.0f / std::sqrt(3.0f);
        const float d = cos_a + (1.0f - cos_a) / 3.0f;
        const float a = (1.0f - cos_a) / 3.0f - sin_a * s3;
        const float b = (1.0f - cos_a) / 3.0f + sin_a * s3;
        out.transform.p[0] = {d, a, b, 0.0f};
        out.transform.p[1] = {b, d, a, 0.0f};
        out.transform.p[2] = {a, b, d, 0.0f};
        out.transform.clamp_unit = true;
        return out;
    }
    case effects::EffectType::Invert: {
        const auto* p = std::get_if<InvertParams>(&instance.params);
        if (!p || !finite_all({p->amount})) return std::nullopt;
        out.honors_clip = true;
        // c + a * (1 - 2c), no clamp, skipped when a <= 0.
        out.transform = p->amount <= 0.0f
            ? identity_affine()
            : scaled_affine(1.0f - 2.0f * p->amount, p->amount, false);
        return out;
    }
    case effects::EffectType::Exposure: {
        const auto* p = std::get_if<ExposureParams>(&instance.params);
        if (!p || !finite_all({p->stops})) return std::nullopt;
        // Software Exposure honors the clip and multiplies straight RGB
        // inside context.clip.
        out.honors_clip = true;
        if (p->stops == 0.0f) { out.transform = identity_affine(); return out; }
        out.transform = scaled_affine(std::exp2(p->stops), 0.0f, false);
        out.transform.straight_alpha = true;
        out.honors_clip = true;
        return out;
    }
    case effects::EffectType::Fill: {
        const auto* p = std::get_if<FillParams>(&instance.params);
        if (!p || !finite_all({p->amount, p->color.r, p->color.g, p->color.b}))
            return std::nullopt;
        // Software Fill honors the clip and works on straight RGB inside
        // context.clip.
        out.honors_clip = true;
        if (p->amount <= 0.0f) { out.transform = identity_affine(); return out; }
        const bool replace = p->mode == FillMode::Replace;
        const float t = p->amount;
        const float keep = replace ? 0.0f : 1.0f - t;
        const float mix = replace ? 1.0f : t;
        out.transform.p[0] = {keep, 0.0f, 0.0f, p->color.r * mix};
        out.transform.p[1] = {0.0f, keep, 0.0f, p->color.g * mix};
        out.transform.p[2] = {0.0f, 0.0f, keep, p->color.b * mix};
        out.transform.straight_alpha = true;
        out.honors_clip = true;
        return out;
    }
    case effects::EffectType::Vignette: {
        const auto* p = std::get_if<VignetteParams>(&instance.params);
        if (!p || !finite_all({p->radius, p->softness, p->amount,
                               p->color.r, p->color.g, p->color.b}))
            return std::nullopt;
        out.transform.mode = NativeColorTransform::Mode::Vignette;
        out.transform.p[0] = {p->radius, p->softness, p->amount, p->color.r};
        out.transform.p[1] = {p->color.g, p->color.b, 0.0f, 0.0f};
        out.honors_clip = true;
        return out;
    }
    case effects::EffectType::Mosaic: {
        const auto* p = std::get_if<MosaicParams>(&instance.params);
        if (!p || p->block_size == 0 || p->block_size > 512)
            return std::nullopt;
        out.transform.mode = NativeColorTransform::Mode::Mosaic;
        out.transform.p[0] = {static_cast<float>(p->block_size),
                              static_cast<float>(origin_x),
                              static_cast<float>(origin_y), 0.0f};
        out.honors_clip = true;
        return out;
    }
    case effects::EffectType::Levels: {
        const auto* p = std::get_if<LevelsParams>(&instance.params);
        if (!p) return std::nullopt;
        const LevelsChannel* lanes[4] = {&p->red, &p->green, &p->blue, &p->master};
        out.transform.mode = NativeColorTransform::Mode::Levels;
        out.transform.straight_alpha = true;
        out.honors_clip = true;
        for (std::size_t lane = 0; lane < 4; ++lane) {
            const auto& c = *lanes[lane];
            if (!finite_all({c.input_black, c.input_white, c.gamma,
                             c.output_black, c.output_white}))
                return std::nullopt;
            out.transform.p[0][lane] = c.input_black;
            out.transform.p[1][lane] = c.input_white;
            out.transform.p[2][lane] = c.gamma;
            out.transform.p[3][lane] = c.output_black;
            out.transform.p[4][lane] = c.output_white;
        }
        out.honors_clip = true;
        return out;
    }
    case effects::EffectType::Threshold: {
        const auto* p = std::get_if<ThresholdParams>(&instance.params);
        if (!p || !finite_all({p->threshold, p->softness})) return std::nullopt;
        if (p->threshold < 0.0f || p->threshold > 1.0f ||
            p->softness < 0.0f || p->softness > 1.0f)
            return std::nullopt;
        out.transform.mode = NativeColorTransform::Mode::Threshold;
        out.transform.straight_alpha = true;
        out.transform.p[0][0] = p->threshold;
        out.transform.p[0][1] = p->softness;
        out.honors_clip = true;
        return out;
    }
    case effects::EffectType::Keying: {
        const auto* p = std::get_if<KeyingParams>(&instance.params);
        if (!p || (p->mode != KeyingMode::Luma && p->mode != KeyingMode::Chroma) ||
            !finite_all({p->threshold, p->softness, p->spill,
                         p->key_color.r, p->key_color.g, p->key_color.b,
                         p->clip_black, p->clip_white, p->choke,
                         p->garbage_x, p->garbage_y, p->garbage_w,
                         p->garbage_h, p->garbage_feather}) ||
            p->threshold < 0.0f || p->threshold > 1.0f ||
            p->softness < 0.0f || p->softness > 1.0f ||
            p->spill < 0.0f || p->spill > 1.0f ||
            p->key_color.r < 0.0f || p->key_color.r > 1.0f ||
            p->key_color.g < 0.0f || p->key_color.g > 1.0f ||
            p->key_color.b < 0.0f || p->key_color.b > 1.0f ||
            p->clip_black < 0.0f || p->clip_white > 1.0f ||
            p->clip_white <= p->clip_black || p->choke < 0.0f ||
            p->choke >= 1.0f || p->garbage_x < 0.0f || p->garbage_x > 1.0f ||
            p->garbage_y < 0.0f || p->garbage_y > 1.0f ||
            p->garbage_w < 0.0f || p->garbage_w > 1.0f ||
            p->garbage_h < 0.0f || p->garbage_h > 1.0f ||
            p->garbage_feather < 0.0f || p->garbage_feather > 0.5f)
            return std::nullopt;
        out.transform.mode = NativeColorTransform::Mode::Keying;
        out.transform.straight_alpha = true;
        out.transform.p[0] = {p->key_color.r, p->key_color.g,
                              p->key_color.b, p->threshold};
        out.transform.p[1] = {p->softness, p->spill,
                              p->clip_black, p->clip_white};
        out.transform.p[2] = {p->choke, p->garbage_x,
                              p->garbage_y, p->garbage_w};
        out.transform.p[3] = {p->garbage_h, p->garbage_feather,
                              0.0f, 0.0f};
        out.transform.p[4][0] = p->mode == KeyingMode::Luma ? 1.0f : 0.0f;
        out.honors_clip = true;
        return out;
    }
    default:
        return std::nullopt;
    }
}

bool try_native_full_frame_color(RenderGraphContext& ctx,
                                 const EffectStack& effect_stack,
                                 Framebuffer& result,
                                 const std::optional<raster::BBox>& clip,
                                 runtime::RenderSurfaceHandle borrowed_source,
                                 const Framebuffer* borrowed_source_framebuffer) {
    if (!ctx.backend || !ctx.surface_registry || effect_stack.size() != 1 ||
        !ctx.backend->supports_native_surfaces())
        return false;
    const auto& instance = effect_stack[0];
    if (!instance.enabled) return false;
    const auto lowering = lower_native_color_effect(
        instance, result.origin_x(), result.origin_y());
    if (!lowering) return false;

    const auto desc = native_surface_desc(result.width(), result.height());
    const bool source_is_borrowed =
        borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool source_needs_expansion = source_is_borrowed && borrowed_source_framebuffer &&
        (borrowed_source_framebuffer->width() != result.width() ||
         borrowed_source_framebuffer->height() != result.height() ||
         borrowed_source_framebuffer->origin_x() != result.origin_x() ||
         borrowed_source_framebuffer->origin_y() != result.origin_y());
    const bool source_is_existing = !source_needs_expansion &&
        (source_is_borrowed || result.surface_handle() != runtime::kInvalidRenderSurfaceHandle);
    if (!source_is_existing && !source_needs_expansion && result.data() == nullptr)
        return false;  // nothing to upload and no native source

    // At most source + output + clip destination: fixed stack ownership list.
    std::array<runtime::RenderSurfaceHandle, 3> owned{};
    std::size_t owned_count = 0;
    const auto make = [&]() {
        const auto h = ctx.surface_registry->create(desc);
        if (h != runtime::kInvalidRenderSurfaceHandle) owned[owned_count++] = h;
        return h;
    };
    const auto release = [&](runtime::RenderSurfaceHandle h) {
        (void)ctx.backend->release_surface(h);
        ctx.surface_registry->release(h);
    };
    const auto cleanup = [&] { for (const auto h : std::span(owned).first(owned_count)) release(h); };

    const auto source = source_is_existing
        ? (source_is_borrowed ? borrowed_source : result.surface_handle())
        : make();
    const auto output = make();
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        output == runtime::kInvalidRenderSurfaceHandle) {
        for (const auto h : std::span(owned).first(owned_count)) ctx.surface_registry->release(h);
        return false;
    }

    RenderOpResult status = source_is_existing
        ? RenderOpResult(RenderOpOutcome{})
        : ctx.backend->create_surface(source, desc);
    if (status.ok() && source_needs_expansion) {
        status = ctx.backend->initialize_transparent_surface(source);
        if (status.ok())
            status = ctx.backend->transform_surface(
                source, borrowed_source,
                borrowed_source_framebuffer->origin_x() - result.origin_x(),
                borrowed_source_framebuffer->origin_y() - result.origin_y(), 1.0f);
    } else if (status.ok() && !source_is_existing) {
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Effects);
        std::vector<float> packed_rgba;
        status = ctx.backend->upload_surface(
            source, desc, framebuffer_rgba_view(result, packed_rgba));
    }
    if (status.ok()) status = ctx.backend->create_surface(output, desc);
    if (status.ok())
        status = ctx.backend->color_transform_surface(output, source, lowering->transform);

    // Clip-honoring processors leave pixels outside the clip untouched.
    auto final_surface = output;
    const bool partial_clip = lowering->honors_clip && clip && !clip_covers_result(clip, result);
    if (status.ok() && partial_clip) {
        raster::BBox local{clip->x0 - result.origin_x(), clip->y0 - result.origin_y(),
                           clip->x1 - result.origin_x(), clip->y1 - result.origin_y()};
        local.clip_to(result.width(), result.height());
        final_surface = make();
        status = final_surface == runtime::kInvalidRenderSurfaceHandle
            ? RenderOpResult(make_render_backend_error(
                  RenderBackendErrorCode::ExecutionFailure,
                  "native color effect: clip surface allocation failed"))
            : ctx.backend->create_surface(final_surface, desc);
        if (status.ok()) status = ctx.backend->copy_surface(final_surface, source);
        if (status.ok() && !local.is_empty())
            status = ctx.backend->copy_surface(final_surface, output, local);
    }
    if (!status.ok()) {
        spdlog::error("[native-color] {} failed: {}",
                      static_cast<int>(instance.effect_type), status.error().message);
        cleanup();
        return false;
    }
    for (const auto h : std::span(owned).first(owned_count))
        if (h != final_surface) release(h);
    // The result's previous native surface (if any) is superseded, matching
    // the blur fast-path ownership contract.
    if (source_is_existing && !source_is_borrowed) release(source);
    result.set_surface_handle(final_surface);
    return true;
}

bool try_native_full_frame_chromatic(RenderGraphContext& ctx,
                                    const EffectStack& effect_stack,
                                    Framebuffer& result,
                                    const std::optional<raster::BBox>& clip,
                                    runtime::RenderSurfaceHandle borrowed_source,
                                    const Framebuffer* borrowed_source_framebuffer) {
    if (!ctx.backend || !ctx.surface_registry || effect_stack.size() != 1 ||
        !ctx.backend->supports_native_surfaces()) return false;
    const auto& instance = effect_stack[0];
    const auto* p = std::get_if<ChromaticAberrationParams>(&instance.params);
    if (!instance.enabled || instance.effect_type != effects::EffectType::ChromaticAberration || !p)
        return false;
    const auto valid_channel = [](const RgbChannelTransform& channel) {
        return std::isfinite(channel.translation.x) && std::isfinite(channel.translation.y) &&
            std::abs(channel.translation.x) <= 2048.0f &&
            std::abs(channel.translation.y) <= 2048.0f &&
            std::isfinite(channel.scale.x) && std::isfinite(channel.scale.y) &&
            channel.scale.x >= 0.01f && channel.scale.x <= 100.0f &&
            channel.scale.y >= 0.01f && channel.scale.y <= 100.0f &&
            std::isfinite(channel.rotation_degrees) &&
            std::abs(channel.rotation_degrees) <= 3600.0f &&
            std::isfinite(channel.opacity) && channel.opacity >= 0.0f && channel.opacity <= 1.0f;
    };
    if (!std::isfinite(p->red_offset) || !std::isfinite(p->blue_offset) ||
        std::abs(p->red_offset) > 128.0f || std::abs(p->blue_offset) > 128.0f ||
        !std::isfinite(p->center.x) || !std::isfinite(p->center.y) ||
        !std::isfinite(p->velocity.x) || !std::isfinite(p->velocity.y) ||
        std::abs(p->center.x) > 1.0e6f || std::abs(p->center.y) > 1.0e6f ||
        std::abs(p->velocity.x) > 1.0e6f || std::abs(p->velocity.y) > 1.0e6f ||
        !valid_channel(p->red) || !valid_channel(p->green) || !valid_channel(p->blue))
        return false;
    if (p->offset_mode != ChromaticOffsetMode::Constant &&
        p->offset_mode != ChromaticOffsetMode::Radial &&
        p->offset_mode != ChromaticOffsetMode::Velocity) return false;

    const auto desc = native_surface_desc(result.width(), result.height());
    const bool source_is_borrowed = borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool source_needs_expansion = source_is_borrowed && borrowed_source_framebuffer &&
        (borrowed_source_framebuffer->width() != result.width() ||
         borrowed_source_framebuffer->height() != result.height() ||
         borrowed_source_framebuffer->origin_x() != result.origin_x() ||
         borrowed_source_framebuffer->origin_y() != result.origin_y());
    const bool source_is_existing = !source_needs_expansion &&
        (source_is_borrowed || result.surface_handle() != runtime::kInvalidRenderSurfaceHandle);
    if (!source_is_existing && !source_needs_expansion && result.data() == nullptr) return false;

    // At most source + output + clip destination: fixed stack ownership list.
    std::array<runtime::RenderSurfaceHandle, 3> owned{};
    std::size_t owned_count = 0;
    const auto make = [&]() {
        const auto handle = ctx.surface_registry->create(desc);
        if (handle != runtime::kInvalidRenderSurfaceHandle) owned[owned_count++] = handle;
        return handle;
    };
    const auto release = [&](runtime::RenderSurfaceHandle handle) {
        (void)ctx.backend->release_surface(handle);
        ctx.surface_registry->release(handle);
    };
    const auto cleanup = [&] { for (const auto handle : std::span(owned).first(owned_count)) release(handle); };

    const auto source = source_is_existing
        ? (source_is_borrowed ? borrowed_source : result.surface_handle()) : make();
    const auto output = make();
    if (source == runtime::kInvalidRenderSurfaceHandle ||
        output == runtime::kInvalidRenderSurfaceHandle) {
        for (const auto handle : std::span(owned).first(owned_count)) ctx.surface_registry->release(handle);
        return false;
    }

    RenderOpResult status = source_is_existing
        ? RenderOpResult(RenderOpOutcome{}) : ctx.backend->create_surface(source, desc);
    if (status.ok() && source_needs_expansion) {
        status = ctx.backend->initialize_transparent_surface(source);
        if (status.ok()) status = ctx.backend->transform_surface(
            source, borrowed_source,
            borrowed_source_framebuffer->origin_x() - result.origin_x(),
            borrowed_source_framebuffer->origin_y() - result.origin_y(), 1.0f);
    } else if (status.ok() && !source_is_existing) {
        profiling::GpuUploadProducerScope upload_scope(profiling::GpuUploadProducer::Effects);
        std::vector<float> packed_rgba;
        status = ctx.backend->upload_surface(source, desc, framebuffer_rgba_view(result, packed_rgba));
    }
    if (status.ok()) status = ctx.backend->create_surface(output, desc);
    if (status.ok()) status = ctx.backend->chromatic_transform_surface(output, source, *p);

    auto final_surface = output;
    const bool partial_clip = clip && !clip_covers_result(clip, result);
    if (status.ok() && partial_clip) {
        raster::BBox local{clip->x0 - result.origin_x(), clip->y0 - result.origin_y(),
                           clip->x1 - result.origin_x(), clip->y1 - result.origin_y()};
        local.clip_to(result.width(), result.height());
        final_surface = make();
        status = final_surface == runtime::kInvalidRenderSurfaceHandle
            ? RenderOpResult(make_render_backend_error(RenderBackendErrorCode::ExecutionFailure,
                  "native chromatic effect: clip surface allocation failed"))
            : ctx.backend->create_surface(final_surface, desc);
        if (status.ok()) status = ctx.backend->copy_surface(final_surface, source);
        if (status.ok() && !local.is_empty())
            status = ctx.backend->copy_surface(final_surface, output, local);
    }
    if (!status.ok()) {
        spdlog::error("[native-chromatic] failed: {}", status.error().message);
        cleanup();
        return false;
    }
    for (const auto handle : std::span(owned).first(owned_count))
        if (handle != final_surface) release(handle);
    if (source_is_existing && !source_is_borrowed) release(source);
    result.set_surface_handle(final_surface);
    return true;
}

} // namespace native_effects
