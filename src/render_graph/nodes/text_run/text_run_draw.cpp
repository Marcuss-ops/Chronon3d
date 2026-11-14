// Cached native MTSDF glyph draw path.
// SPDX-License-Identifier: MIT
//
// gpu_text_run_mtsdf.cpp — MTSDF preparation and cached dispatch for the GPU
// text-run family.
//
// Split out of gpu_text_run.cpp: it builds the persistent MTSDF glyph-atlas
// representation of a shaped run (prepare_mtsdf_text_run) and dispatches the
// already-prepared batches straight from the runtime prepared cache
// (draw_cached_text_run_mtsdf). See gpu_text_run.hpp for the contract.

#include "gpu_text_run.hpp"
#include "gpu_text_run_detail.hpp"

#include "../native_surface.hpp"

#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/runtime/gpu_text_selector_metadata.hpp>
#include <chronon3d/text/mtsdf_preparation.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>
#include <chronon3d/text/gel_surface.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::text_run {

namespace {

/// Frame-transient scratch surface for the run-level canonical shadow. The
/// surface is registered with the backend and released in reverse order; all
/// recorded passes keep the handle alive until the batch retires it.
struct TextScratchSurface {
    RenderGraphContext* ctx{nullptr};
    runtime::RenderSurfaceHandle handle{runtime::kInvalidRenderSurfaceHandle};

    TextScratchSurface() = default;
    TextScratchSurface(const TextScratchSurface&) = delete;
    TextScratchSurface& operator=(const TextScratchSurface&) = delete;
    ~TextScratchSurface() { reset(); }

    bool create(RenderGraphContext& context, std::uint32_t width,
                std::uint32_t height) {
        ctx = &context;
        const auto desc = runtime::SurfaceDesc::make(
            width, height, runtime::canonical_render_format(),
            runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient);
        handle = ctx->surface_registry->create(desc);
        if (handle == runtime::kInvalidRenderSurfaceHandle) return false;
        const auto created = ctx->backend_surface_api()->create_surface(handle, desc);
        if (!created.ok()) {
            (void)ctx->surface_registry->release(handle);
            handle = runtime::kInvalidRenderSurfaceHandle;
            return false;
        }
        return true;
    }

    void reset() noexcept {
        if (ctx == nullptr || handle == runtime::kInvalidRenderSurfaceHandle) return;
        (void)ctx->backend_surface_api()->release_surface(handle);
        (void)ctx->surface_registry->release(handle);
        handle = runtime::kInvalidRenderSurfaceHandle;
    }
};

} // namespace

graph::RenderOpResult draw_cached_text_run_mtsdf(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    const TextRunShape& shape,
    const glm::mat4& model_matrix,
    float opacity) {
    // Empty typewriter frames are valid: a zero-glyph run is a successful
    // no-op, not an unsupported native capability.  This is the GPU half of
    // the T2-02 frame-0 contract; the software rasterizer mirrors it.
    if (shape.glyphs.empty()) {
        spdlog::info("[text-route] frame={} route=mtsdf-empty",
                     ctx.frame_input.frame.integral());
        return graph::RenderOpResult(graph::RenderOpOutcome{0});
    }
    if (!ctx.gpu_glyph_atlas || !ctx.gpu_text_prepared_cache ||
        !ctx.asset_resolver || !ctx.backend ||
        !ctx.surface_registry || !shape.layout) {
        spdlog::warn(
            "[text-route] frame={} route=mtsdf reason=missing-native-contract",
            ctx.frame_input.frame.integral());
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route does not support this run"));
    }
    // A GPU success must never silently drop a per-glyph state the MTSDF
    // shader cannot represent.
    if (!shape.animators.empty() && !can_lower_gpu_text_animation(shape)) {
        spdlog::warn(
            "[text-route] frame={} route=mtsdf reason=unsupported-animator",
            ctx.frame_input.frame.integral());
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route does not support this animator stack"));
    }

    const auto quality_key = static_cast<std::uint8_t>(shape.paint.quality);
    // Vector strokes use a separate coverage atlas whose pixels encode fill
    // and stroke masks, not an MTSDF distance field.  The prepared batch is
    // deliberately rebuilt per submission: the expensive outline expansion and
    // rasterisation are cached by GpuGlyphKey, while the batch itself must not
    // be shared with a differently styled run in the layout-only cache.
    const bool vector_coverage =
        shape.paint.stroke_enabled &&
        shape.paint.stroke_mode == TextStrokeMode::Vector;
    std::shared_ptr<const runtime::GpuPreparedTextRun> prepared;
    if (vector_coverage) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_cache_misses.fetch_add(1, std::memory_order_relaxed);
            profiling::g_current_counters->gpu_text_prepared_batch_build_count.fetch_add(1, std::memory_order_relaxed);
        }
        const auto prep_t0 = profiling::g_current_counters ? profiling::now() : profiling::Clock::time_point{};
        prepared = prepare_vector_stroke_text_run(
            *ctx.gpu_glyph_atlas, shape);
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_prepare_per_frame_wall_us.fetch_add(
                static_cast<uint64_t>(std::llround(profiling::elapsed_us(prep_t0))),
                std::memory_order_relaxed);
        }
        if (!prepared || !prepared->ready) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "Vector text route has no resident coverage glyphs"));
        }
    } else {
        prepared = ctx.gpu_text_prepared_cache->find(*shape.layout, quality_key);
        if (!prepared) {
            if (profiling::g_current_counters) {
                profiling::g_current_counters->text_cache_misses.fetch_add(1, std::memory_order_relaxed);
                profiling::g_current_counters->gpu_text_prepared_batch_build_count.fetch_add(1, std::memory_order_relaxed);
            }
            const auto prep_t0 = profiling::g_current_counters ? profiling::now() : profiling::Clock::time_point{};
            auto created = prepare_mtsdf_text_run(
                *ctx.gpu_glyph_atlas, ctx.asset_resolver, shape);
            if (profiling::g_current_counters) {
                profiling::g_current_counters->text_prepare_per_frame_wall_us.fetch_add(
                    static_cast<uint64_t>(std::llround(profiling::elapsed_us(prep_t0))),
                    std::memory_order_relaxed);
            }
            if (!created || !created->ready) {
                return graph::RenderOpResult(graph::make_render_backend_error(
                    graph::RenderBackendErrorCode::UnsupportedCapability,
                    "MTSDF text route has no resident glyphs"));
            }
            ctx.gpu_text_prepared_cache->store(*shape.layout, quality_key, created);
            prepared = created;
        } else {
            if (profiling::g_current_counters) {
                profiling::g_current_counters->text_cache_hits.fetch_add(1, std::memory_order_relaxed);
                profiling::g_current_counters->gpu_text_prepared_batch_reuse_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (prepared->atlas_generation != ctx.gpu_glyph_atlas->generation()) {
        // The atlas advanced its residency epoch (clear/reattach) after this
        // run was prepared. Rebuild once through the canonical preparation
        // path; a stale batch must never bind retired pages.
        spdlog::warn(
            "[text-cache] rebuilding prepared MTSDF batch after glyph-atlas epoch change");
        ctx.gpu_text_prepared_cache->clear();
        auto rebuilt = vector_coverage
            ? prepare_vector_stroke_text_run(*ctx.gpu_glyph_atlas, shape)
            : prepare_mtsdf_text_run(
                *ctx.gpu_glyph_atlas, ctx.asset_resolver, shape);
        if (!rebuilt || !rebuilt->ready) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "Text route could not rebuild after glyph-atlas epoch change"));
        }
        if (!vector_coverage)
            ctx.gpu_text_prepared_cache->store(*shape.layout, quality_key, rebuilt);
        prepared = std::move(rebuilt);
    }

    if (!prepared->ready) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route has no resident glyphs"));
    }

    const auto pages_are_bound = [&](
        const runtime::GpuPreparedTextRun& candidate) {
        return std::all_of(candidate.batches.begin(), candidate.batches.end(),
            [&](const runtime::GpuPreparedTextRun::Batch& batch) {
                return batch.page != runtime::kInvalidRenderSurfaceHandle &&
                    ctx.surface_registry->lookup(batch.page).has_value() &&
                    ctx.backend_surface_api()->is_native_surface_valid(batch.page);
            });
    };
    if (!pages_are_bound(*prepared)) {
        // The daemon keeps text caches alive across render jobs, while each
        // job may retire/recreate the Vulkan bindings behind their numeric
        // page handles. Drop both cached layout batches and atlas residency,
        // then rebuild the GPU atlas against the current surface registry.
        spdlog::warn(
            "[text-cache] invalidating prepared MTSDF pages after backend surface retirement");
        ctx.gpu_text_prepared_cache->clear();
        ctx.gpu_glyph_atlas->clear();
        auto rebuilt = vector_coverage
            ? prepare_vector_stroke_text_run(*ctx.gpu_glyph_atlas, shape)
            : prepare_mtsdf_text_run(
                *ctx.gpu_glyph_atlas, ctx.asset_resolver, shape);
        if (!rebuilt || !rebuilt->ready || !pages_are_bound(*rebuilt)) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "Text route could not rebuild retired GPU atlas pages"));
        }
        if (!vector_coverage)
            ctx.gpu_text_prepared_cache->store(*shape.layout, quality_key, rebuilt);
        prepared = std::move(rebuilt);
    }

    const auto& layout = *shape.layout;
    const std::uint16_t kRangePx = (shape.paint.quality == TextQuality::High
        ? text::kHighQualityMtsdfProfile : text::kCanonicalMtsdfProfile)
        .distance_range_px;

    runtime::TextRunDynamic run{};
    run.tx = model_matrix[3][0];
    run.ty = model_matrix[3][1];
    run.sx = model_matrix[0][0];
    run.sy = model_matrix[1][1];
    // The MTSDF shader stores the layer transform as scale + translation.
    // Preserve an in-plane layer rotation by decomposing its orthogonal XY
    // basis and applying that rotation around each glyph's layout origin.
    // The per-glyph offset below carries the difference between the full
    // affine-transformed origin and the old diagonal-only origin. Without it,
    // the off-diagonal terms were dropped and a rotated/scaled text layer
    // orbited the canvas origin, disappearing during combined zoom sweeps.
    const float basis_xx = model_matrix[0][0];
    const float basis_xy = model_matrix[0][1];
    const float basis_yx = model_matrix[1][0];
    const float basis_yy = model_matrix[1][1];
    const float basis_x_len = std::hypot(basis_xx, basis_xy);
    const float basis_y_len = std::hypot(basis_yx, basis_yy);
    const float basis_dot = basis_xx * basis_yx + basis_xy * basis_yy;
    const bool affine_xy = std::abs(model_matrix[0][3]) < 1e-6f &&
        std::abs(model_matrix[1][3]) < 1e-6f &&
        std::abs(model_matrix[2][3]) < 1e-6f &&
        std::abs(model_matrix[3][3] - 1.0f) < 1e-6f;
    const bool rotated_xy = affine_xy && basis_x_len > 1e-6f &&
        basis_y_len > 1e-6f && std::abs(basis_dot) <=
            1e-4f * basis_x_len * basis_y_len &&
        (std::abs(basis_xy) > 1e-6f || std::abs(basis_yx) > 1e-6f);
    const float layer_rotation_z = rotated_xy
        ? std::atan2(basis_xy, basis_xx) * (180.0f / 3.14159265358979323846f)
        : 0.0f;
    if (rotated_xy) {
        const float determinant = basis_xx * basis_yy - basis_yx * basis_xy;
        run.sx = basis_x_len;
        run.sy = determinant / basis_x_len;
    }
    run.opacity = opacity;
    run.color = ::chronon3d::text::pack_text_color(shape.paint.fill);
    // Keep the shader interpretation tied to the atlas profile rather than
    // duplicating the distance range as a magic constant in GLSL.
    run.distance_range_bits = std::bit_cast<std::uint32_t>(
        static_cast<float>(kRangePx));
    run.visible_glyph_count = std::numeric_limits<std::uint32_t>::max();
    if (shape.material.gel_surface) {
        run.effect_flags |= (1u << 3);
        run.effect_flags |= (1u << 4); // animator_reserved5 carries gel light params.
        if (shape.material.gel_core_only) {
            run.effect_flags |= (1u << 5);
        }
        if (shape.material.gel_transmission_enabled) {
            run.effect_flags |= (1u << 9);
        }
        if (shape.material.gel_soft_fusion) {
            run.effect_flags |= (1u << 10);
        }
        if (shape.material.gel_reference_20261107) {
            run.effect_flags |= kTextRunEffectGelReference;
        }
        run.effect_flags |= (static_cast<std::uint32_t>(
            shape.material.gel_debug_mode & 0x7u) << 6u);
        run.animator_reserved5 = ::chronon3d::text::gel::pack_light_parameters(
            shape.material.gel_light_azimuth_radians,
            shape.material.gel_light_elevation);
        sample_gpu_gel_light_animation(
            shape, ctx.frame_input.sample_time, run);
    }
    if (vector_coverage) {
        // Vector coverage is already an expanded outline: do not ask the
        // distance-field shader to dilate it a second time.  Bit 0 of run.flags
        // still selects plane placement, not MTSDF sampling.
        run.effect_flags |= runtime::kTextRunEffectVectorCoverage;
        run.flags |= 1u;
        run.stroke_width = shape.paint.stroke_width;
        run.stroke_color = ::chronon3d::text::pack_text_color(shape.paint.stroke_color);
    } else if (shape.paint.stroke_enabled && shape.paint.stroke_width > 0.0f) {
        run.effect_flags |= 1u;
        run.stroke_width = shape.paint.stroke_width;
        run.stroke_color = ::chronon3d::text::pack_text_color(shape.paint.stroke_color);
    }
    // Layer shadows are ordered: the first authored shadow sits under the
    // second and the fill sits above all of them.  A single shadow keeps the
    // one-pass fast path; multiple shadows are emitted as ordered shadow-only
    // passes followed by one fill pass.  The GPU limit is declared and
    // fail-closed instead of silently dropping an authored layer.
    std::vector<const TextShadow*> gpu_shadows;
    gpu_shadows.reserve(shape.shadows.size());
    for (const auto& shadow : shape.shadows) {
        if (shadow.enabled) gpu_shadows.push_back(&shadow);
    }
    if (gpu_shadows.size() > runtime::kTextRunMaxGpuShadowLayers) {
        spdlog::warn(
            "[text-route] frame={} route=mtsdf reason=too-many-shadow-layers "
            "count={} limit={}",
            ctx.frame_input.frame.integral(), gpu_shadows.size(),
            runtime::kTextRunMaxGpuShadowLayers);
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route supports at most 4 ordered shadow layers"));
    }
    const auto apply_shadow = [](runtime::TextRunDynamic& target,
                                 const TextShadow& shadow) {
        Color shadow_color = shadow.color;
        shadow_color.a = std::clamp(shadow_color.a * shadow.opacity, 0.0f, 1.0f);
        target.shadow_color = ::chronon3d::text::pack_text_color(shadow_color);
        target.shadow_dx = shadow.offset.x;
        target.shadow_dy = shadow.offset.y;
        target.shadow_blur = shadow.blur;
        target.effect_flags |= (std::abs(shadow.offset.x) < 1e-4f &&
                                std::abs(shadow.offset.y) < 1e-4f) ? 4u : 2u;
    };
    const bool canonical_shadow =
        shape.paint.shadow_quality == TextShadowQuality::Canonical &&
        !gpu_shadows.empty();
    if (!canonical_shadow && gpu_shadows.size() == 1)
        apply_shadow(run, *gpu_shadows.front());
    const auto shadow_only_run = [&](const TextShadow& shadow) {
        runtime::TextRunDynamic shadow_run = run;
        shadow_run.effect_flags &= ~(2u | 4u);
        apply_shadow(shadow_run, shadow);
        shadow_run.effect_flags |= runtime::kTextRunEffectShadowOnly;
        shadow_run.caret_flags = 0;
        return shadow_run;
    };
    if (shape.caret.enabled) {
        const TextCaretRect caret = resolve_text_caret_rect(shape);
        if (caret.visible && caret.width > 0.0f && caret.height > 0.0f) {
            Color caret_color = shape.caret.color;
            caret_color.a = std::clamp(
                caret_color.a * shape.caret.opacity, 0.0f, 1.0f);
            run.caret_color = ::chronon3d::text::pack_text_color(caret_color);
            run.caret_x = caret.x;
            run.caret_y = caret.y;
            run.caret_width = caret.width;
            run.caret_height = caret.height;
            run.caret_flags = 1u;
        }
    }

    if (!ensure_empty_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route destination has no native surface"));
    }
    std::size_t items_drawn = 0;
    const auto draw_t0 = profiling::g_current_counters ? profiling::now() : profiling::Clock::time_point{};
    const auto dispatch_batch = [&](
        const runtime::GpuPreparedTextRun::Batch& batch,
        const runtime::TextRunDynamic& pass_run,
        runtime::RenderSurfaceHandle target,
        bool union_mask) -> graph::RenderOpResult {
        if (batch.glyphs.empty())
            return graph::RenderOpResult(graph::RenderOpOutcome{0});
        const bool has_resolved_glyph_motion = rotated_xy || !shape.animators.empty() ||
            std::any_of(shape.glyphs.begin(), shape.glyphs.end(),
                [](const GlyphInstanceState& glyph) {
                    return glyph.opacity != 1.0f || glyph.position.x != 0.0f ||
                           glyph.position.y != 0.0f || glyph.position.z != 0.0f ||
                           glyph.baseline_shift != 0.0f ||
                           glyph.anchor.x != 0.0f || glyph.anchor.y != 0.0f ||
                           glyph.rotation.x != 0.0f || glyph.rotation.y != 0.0f ||
                           glyph.rotation.z != 0.0f || glyph.skew != 0.0f ||
                           glyph.stroke.a != 0.0f || glyph.background.a != 0.0f ||
                           glyph.shadow.enabled || glyph.glow.enabled ||
                           glyph.fill_gradient.enabled ||
                           glyph.scale.x != 1.0f ||
                           glyph.scale.y != 1.0f || glyph.scale.z != 1.0f ||
                           glyph.blur != 0.0f ||
                           glyph.fill.r != 1.0f || glyph.fill.g != 1.0f ||
                           glyph.fill.b != 1.0f || glyph.fill.a != 1.0f;
                });
        const auto dispatch = [&]() -> graph::RenderOpResult {
          if (!has_resolved_glyph_motion && !union_mask) {
            return ctx.backend_texture_api()->draw_text_batch(
                target, batch.glyphs,
                std::span<const runtime::TextRunDynamic>(&pass_run, 1),
                std::span<const runtime::RenderSurfaceHandle>(&batch.page, 1),
                std::span<const runtime::GlyphPaint>{});
          }
            // Reuse the immutable atlas batch, but bind each glyph to its own
            // resolved dynamic record. This is the GPU compatibility path for
            // the complete Chronon animator evaluator: arbitrary selectors,
            // expression tracks and stacked animators have already been
            // reduced to the final GlyphInstanceState on the CPU. Geometry,
            // coverage, effects and compositing remain GPU rendered.
            std::vector<runtime::GlyphStatic> glyphs(batch.glyphs.begin(), batch.glyphs.end());
            std::vector<runtime::TextRunDynamic> glyph_runs(
                shape.glyphs.size(), pass_run);
            const bool has_fill_animator = std::any_of(
                shape.animators.begin(), shape.animators.end(),
                [](const TextAnimatorSpec& animator) {
                    return std::any_of(
                        animator.properties.begin(), animator.properties.end(),
                        [](const TextAnimatorProperty& property) {
                            return std::holds_alternative<FillColorProperty>(property);
                        });
                });
            std::vector<runtime::GlyphPaint> glyph_paints(glyphs.size());
            for (std::size_t glyph_index = 0; glyph_index < glyphs.size(); ++glyph_index) {
                auto& glyph = glyphs[glyph_index];
                const std::size_t state_index = glyph.pad0;
                if (state_index >= shape.glyphs.size()) {
                    return graph::RenderOpResult(graph::make_render_backend_error(
                        graph::RenderBackendErrorCode::InvalidInput,
                        "MTSDF batch glyph index exceeds resolved glyph state"));
                }
                const auto& state = shape.glyphs[state_index];
                auto& paint = glyph_paints[glyph_index];
                if (state.background.a > 0.0f) {
                    paint.flags |= 1u;
                    paint.background_color =
                        ::chronon3d::text::pack_text_color(state.background);
                    const float em = shape.layout->font_size;
                    paint.background_padding_px = std::max(
                        0.0f, state.background_padding_em * em);
                    paint.background_radius_px = std::max(
                        0.0f, state.background_radius_em * em);
                }
                if (std::abs(state.skew) > 1e-6f) {
                    paint.flags |= 2u;
                    constexpr double kPi = 3.14159265358979323846;
                    const double skew_rad = static_cast<double>(state.skew) *
                        (kPi / 180.0);
                    const double axis_rad = static_cast<double>(state.skew_axis) *
                        (kPi / 180.0);
                    paint.skew_tan = static_cast<float>(std::tan(skew_rad));
                    paint.skew_axis_cos = static_cast<float>(std::cos(axis_rad));
                    paint.skew_axis_sin = static_cast<float>(std::sin(axis_rad));
                }
                if (state.fill_gradient.enabled) {
                    paint.flags |= 16u;
                    paint.gradient_start_color = ::chronon3d::text::pack_text_color(
                        state.fill_gradient.start);
                    paint.gradient_end_color = ::chronon3d::text::pack_text_color(
                        state.fill_gradient.end);
                    paint.gradient_angle_degrees =
                        state.fill_gradient.angle_degrees;
                }
                if (state.shadow.enabled) {
                    paint.flags |= 4u;
                    paint.shadow_color =
                        ::chronon3d::text::pack_text_color(state.shadow.color);
                    paint.shadow_dx = state.shadow.offset.x;
                    paint.shadow_dy = state.shadow.offset.y;
                    paint.shadow_blur = state.shadow.blur;
                    paint.shadow_opacity = state.shadow.opacity;
                }
                if (state.glow.enabled) {
                    paint.flags |= 8u;
                    paint.glow_color =
                        ::chronon3d::text::pack_text_color(state.glow.color);
                    paint.glow_radius = state.glow.radius;
                    paint.glow_intensity = state.glow.intensity;
                }
                glyph.run_index = static_cast<std::uint32_t>(state_index);
                glyph.pad0 = 0; // the record selector addresses one local glyph

                auto& dynamic = glyph_runs[state_index];
                // T2-01: a source span (or a transition keyframe paint range)
                // seeds GlyphInstanceState::fill.  The fast batch path can
                // carry that value only through the per-glyph dynamic record;
                // without this copy the run colour painted every glyph and
                // spans silently disappeared on the native route.  A fill
                // animator still owns its own animator_fill_color path below.
                const auto& placed_glyph =
                    layout.placed.glyphs[state_index];
                if (!has_fill_animator && placed_glyph.fill_color_override) {
                    dynamic.color = ::chronon3d::text::pack_text_color(
                        state.fill);
                }
                float depth_factor = 1.0f;
                if (std::isfinite(state.position.z) && state.position.z > 0.0f) {
                    depth_factor = 1.0f / (1.0f + state.position.z * 0.001f);
                }
                const float scale_x = state.scale.x * state.scale.z * depth_factor;
                const float scale_y = state.scale.y * state.scale.z * depth_factor;
                dynamic.flags |= 2u;
                dynamic.flags = runtime::pack_text_selector_metadata(
                    dynamic.flags,
                    static_cast<std::uint32_t>(TextSelectorUnit::Glyph),
                    static_cast<std::uint32_t>(TextSelectorOrder::Forward),
                    TextSelectorShape::Square);
                dynamic.selector_start = 0.0f;
                dynamic.selector_end = 100.0f;
                dynamic.selector_amount = 100.0f;
                dynamic.selector_unit_count = 1u;
                // Pivot compensation for scale, rotation and shear: bake
                // run.s * (I - scale * rotation * shear) * anchor so the
                // shader transform leaves layout_position+anchor fixed.
                const auto pivot_offset = gpu_anchor_pivot_offset(
                    state, scale_x, scale_y,
                    state.rotation.z + layer_rotation_z, run.sx, run.sy);
                dynamic.animator_position_x =
                    state.position.x + pivot_offset.x;
                dynamic.animator_position_y = state.position.y +
                    state.baseline_shift + pivot_offset.y;
                if (rotated_xy) {
                    const glm::vec4 mapped_origin = model_matrix * glm::vec4(
                        glyph.origin_x, glyph.origin_y, 0.0f, 1.0f);
                    const float inv_w = std::abs(mapped_origin.w) > 1e-6f
                        ? 1.0f / mapped_origin.w : 1.0f;
                    const float diagonal_origin_x = run.tx +
                        run.sx * glyph.origin_x;
                    const float diagonal_origin_y = run.ty +
                        run.sy * glyph.origin_y;
                    dynamic.animator_position_x +=
                        mapped_origin.x * inv_w - diagonal_origin_x;
                    dynamic.animator_position_y +=
                        mapped_origin.y * inv_w - diagonal_origin_y;
                }
                dynamic.animator_scale_x = scale_x;
                dynamic.animator_scale_y = scale_y;
                dynamic.animator_rotation_z = state.rotation.z + layer_rotation_z;
                dynamic.animator_rotation_x = state.rotation.x;
                dynamic.animator_rotation_y = state.rotation.y;
                if (gpu_uses_exact_3d_rotation(state)) {
                    dynamic.effect_flags |= kTextRunEffectExact3DRotation;
                }
                constexpr double kPi = 3.14159265358979323846;
                const double ry_rad = static_cast<double>(state.rotation.y) * (kPi / 180.0);
                const double rx_rad = static_cast<double>(state.rotation.x) * (kPi / 180.0);
                const double ry_clamped = std::clamp(ry_rad, -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
                const double rx_clamped = std::clamp(rx_rad, -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
                dynamic.animator_shear_x = static_cast<float>(std::tan(ry_clamped));
                dynamic.animator_shear_y = static_cast<float>(std::tan(rx_clamped));
                dynamic.animator_opacity = state.opacity;
                dynamic.animator_blur = std::max(0.0f, state.blur);
                // Per-glyph stroke is expressed by the same dynamic record
                // the MTSDF shader already consumes for run-level outlines.
                // Keep the run-level paint as the fallback for glyphs whose
                // selector did not enable a per-glyph stroke.
                if (state.stroke.a > 0.0f && state.stroke_width > 0.0f) {
                    if (!vector_coverage) dynamic.effect_flags |= 1u;
                    dynamic.stroke_color =
                        ::chronon3d::text::pack_text_color(state.stroke);
                    dynamic.stroke_width = state.stroke_width;
                }
                if (has_fill_animator) {
                    dynamic.flags |= runtime::kTextRunFlagAnimatorFill;
                    dynamic.animator_fill_color =
                        ::chronon3d::text::pack_text_color(state.fill);
                }
            }
            return ctx.backend_texture_api()->draw_text_batch(
                target, glyphs, glyph_runs,
                std::span<const runtime::RenderSurfaceHandle>(&batch.page, 1),
                std::span<const runtime::GlyphPaint>(glyph_paints));
        }();
        return dispatch;
    };

    // Ordered compositing: every authored shadow first, then a single fill
    // pass.  A shadow-only pass is forbidden from drawing the caret or fill.
    runtime::TextRunDynamic fill_run = run;
    if (canonical_shadow) {
        // One union silhouette per authored shadow. The tile raster writes the
        // per-pixel max coverage; the two blur passes are separable and the
        // final composite is a single linear source-over. This is the GPU twin
        // of the CPU canonical shadow without accumulating per-glyph edges.
        fill_run.effect_flags &= ~(2u | 4u | runtime::kTextRunEffectShadowOnly);
        for (const TextShadow* shadow : gpu_shadows) {
            if (!shadow->enabled || shadow->opacity <= 0.0f) continue;
            TextScratchSurface mask;
            TextScratchSurface blurred;
            if (!mask.create(ctx, static_cast<std::uint32_t>(destination.width()),
                             static_cast<std::uint32_t>(destination.height())) ||
                !blurred.create(ctx, static_cast<std::uint32_t>(destination.width()),
                                static_cast<std::uint32_t>(destination.height()))) {
                return graph::RenderOpResult(graph::make_render_backend_error(
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "MTSDF canonical shadow could not allocate scratch surfaces"));
            }
            runtime::TextRunDynamic mask_run = run;
            mask_run.effect_flags =
                (run.effect_flags & (kTextRunEffectExact3DRotation |
                                     runtime::kTextRunEffectVectorCoverage)) |
                runtime::kTextRunEffectCanonicalShadowMask;
            mask_run.color = ::chronon3d::text::pack_text_color(shadow->color);
            mask_run.opacity = std::clamp(
                opacity * shadow->opacity, 0.0f, 1.0f);
            mask_run.tx += shadow->offset.x;
            mask_run.ty += shadow->offset.y;
            mask_run.shadow_color = 0;
            mask_run.shadow_dx = 0.0f;
            mask_run.shadow_dy = 0.0f;
            mask_run.shadow_blur = 0.0f;
            mask_run.stroke_width = 0.0f;
            mask_run.animator_blur = 0.0f;
            mask_run.caret_flags = 0;
            for (const auto& batch : prepared->batches) {
                if (batch.glyphs.empty()) continue;
                auto pass = dispatch_batch(
                    batch, mask_run, mask.handle, /*union_mask=*/true);
                if (!pass.ok()) return pass;
                items_drawn += pass.value().items_drawn;
            }
            if (shadow->blur > 0.0f) {
                // CPU canonical uses sigma = radius / 3; the generic Vulkan
                // blur shader consumes sigma directly and clamps to 0.5.
                const float sigma = std::max(shadow->blur / 3.0f, 0.5f);
                auto horizontal = ctx.backend_surface_api()->blur_surface(
                    blurred.handle, mask.handle, sigma, /*horizontal=*/true);
                if (!horizontal.ok()) return horizontal;
                auto vertical = ctx.backend_surface_api()->blur_surface(
                    mask.handle, blurred.handle, sigma, /*horizontal=*/false);
                if (!vertical.ok()) return vertical;
            }
            auto composite = ctx.backend_surface_api()->composite_surfaces(
                destination.surface_handle(), mask.handle,
                BlendMode::Normal, CompositeOperator::SourceOver);
            if (!composite.ok()) return composite;
        }
    } else if (gpu_shadows.size() > 1) {
        fill_run.effect_flags &= ~(2u | 4u);
        for (const TextShadow* shadow : gpu_shadows) {
            const runtime::TextRunDynamic shadow_run = shadow_only_run(*shadow);
            for (const auto& batch : prepared->batches) {
                if (batch.glyphs.empty()) continue;
                auto pass = dispatch_batch(
                    batch, shadow_run, destination.surface_handle(), false);
                if (!pass.ok()) return pass;
                items_drawn += pass.value().items_drawn;
            }
        }
    }
    for (const auto& batch : prepared->batches) {
        if (batch.glyphs.empty()) continue;
        auto pass = dispatch_batch(
            batch, fill_run, destination.surface_handle(), false);
        if (!pass.ok()) return pass;
        items_drawn += pass.value().items_drawn;
        // The caret is a single run-level layer: only the first prepared page
        // may draw it, otherwise a multi-page run would paint it repeatedly.
        fill_run.caret_flags = 0;
    }
    if (profiling::g_current_counters) {
        profiling::g_current_counters->text_draw_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_us(draw_t0))),
            std::memory_order_relaxed);
    }
    // Positive route evidence for the pixel gate: a successful draw must
    // identify the native path, not merely omit a fallback warning.
    spdlog::info("[text-route] frame={} route=mtsdf",
                 ctx.frame_input.frame.integral());
    return graph::RenderOpResult(graph::RenderOpOutcome{items_drawn});
}


} // namespace chronon3d::graph::text_run
