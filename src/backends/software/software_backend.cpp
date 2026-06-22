#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/utils/effects/per_pixel_dof.hpp>
#include <chronon3d/backends/software/text_run_processor.hpp>  // 06 R3b wire-through
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/trace_categories.hpp>
#include <chronon3d/scene/model/layer/layer_effect.hpp>
#include <algorithm>
#include <optional>
#include <chronon3d/scene/model/layer/layer_effect.hpp>

namespace chronon3d {
    namespace raster { struct BBox; }
}

namespace chronon3d::renderer {
void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip, int passes);
void apply_color_effects(Framebuffer& fb, const LayerEffect& effect, const std::optional<raster::BBox>& clip);
void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, const effects::EffectExecutionContext& context);
}

namespace chronon3d {

namespace {

std::optional<raster::BBox> to_local_clip(const Framebuffer& fb, std::optional<raster::BBox> clip) {
    if (!clip) return std::nullopt;
    raster::BBox c = *clip;
    c.x0 -= fb.origin_x();
    c.x1 -= fb.origin_x();
    c.y0 -= fb.origin_y();
    c.y1 -= fb.origin_y();
    return c;
}

uint64_t clipped_area(int32_t width, int32_t height, const std::optional<raster::BBox>& clip) {
    uint64_t area = static_cast<uint64_t>(std::max(0, width)) * static_cast<uint64_t>(std::max(0, height));
    if (!clip) return area;
    raster::BBox local_clip = *clip;
    local_clip.clip_to(width, height);
    if (local_clip.is_empty()) return 0;
    return static_cast<uint64_t>(std::max(0, local_clip.x1 - local_clip.x0)) *
           static_cast<uint64_t>(std::max(0, local_clip.y1 - local_clip.y0));
}

} // namespace

// ── Construction ──────────────────────────────────────────────────────────

SoftwareBackend::SoftwareBackend(
    class SoftwareRenderer*            owner,
    RenderCounters&                    counters,
    const RenderSettings&              settings,
    std::shared_ptr<cache::FramebufferPool> pool)
    : m_counters(counters)
    , m_settings(settings)
    , m_framebuffer_pool(std::move(pool))
    , m_owner(owner)
{}

SoftwareBackend::~SoftwareBackend() = default;

// ── draw_node (stub — delegated to SoftwareRenderer until ShapeProcessor migration) ──

void SoftwareBackend::draw_node(Framebuffer&, const RenderNode&, const RenderState&,
                                 const Camera&, int, int) {
    // Intentionally empty: draw_node lives on SoftwareRenderer because
    // ShapeProcessor::draw() currently takes SoftwareRenderer & (not RenderBackend&).
    // This override satisfies the pure-virtual contract from RenderBackend.
}

// ── capabilities ──────────────────────────────────────────────────────────

graph::RenderCapabilities SoftwareBackend::capabilities() const noexcept {
    return graph::RenderCapabilities{
#ifdef CHRONON3D_USE_BLEND2D
        .text_run = true
#else
        .text_run = false
#endif
    };
}

// ── apply_blur ────────────────────────────────────────────────────────────

void SoftwareBackend::apply_blur(Framebuffer& fb, f32 radius,
                                  const std::optional<raster::BBox>& clip) {
    const auto local_clip = to_local_clip(fb, clip);
    m_counters.blur_pixels.fetch_add(
        clipped_area(fb.width(), fb.height(), local_clip),
        std::memory_order_relaxed);
    CHRONON_ZONE_C("apply_blur", trace_category::kEffect);
    renderer::apply_blur(fb, radius, local_clip, 2);
}

// ── composite_layer ───────────────────────────────────────────────────────

void SoftwareBackend::composite_layer(
    Framebuffer& dst, const Framebuffer& src, BlendMode mode,
    const std::optional<raster::BBox>& clip, CompositeOperator op) {
    m_counters.layers_rendered.fetch_add(1, std::memory_order_relaxed);
    CHRONON_ZONE_C("composite_layer", trace_category::kComposite);
    m_counters.pixels_touched.fetch_add(
        clipped_area(dst.width(), dst.height(), to_local_clip(dst, clip)),
        std::memory_order_relaxed);
    SoftwareCompositor::composite_layer(dst, src, mode, clip, op,
        m_settings.force_scalar_normal_blend);
}

// ── apply_effect_stack ────────────────────────────────────────────────────

void SoftwareBackend::apply_effect_stack(
    Framebuffer& fb, const EffectStack& stack,
    const effects::EffectExecutionContext& context) {
    CHRONON_ZONE_C("apply_effect_stack", trace_category::kEffect);

    const auto local_clip = context.clip.has_value()
        ? to_local_clip(fb, context.clip)
        : std::nullopt;
    const uint64_t area = clipped_area(fb.width(), fb.height(), local_clip);
    for (const auto& effect : stack) {
        if (effect.enabled && effect.effect_type == effects::EffectType::Blur) {
            m_counters.blur_pixels.fetch_add(area, std::memory_order_relaxed);
        }
    }

    effects::EffectExecutionContext local_context = context;
    local_context.clip = local_clip;
    renderer::apply_effect_stack(fb, stack, local_context);
}

// ── apply_per_pixel_dof ───────────────────────────────────────────────────

void SoftwareBackend::apply_per_pixel_dof(
    Framebuffer& framebuffer,
    std::span<const float> depth,
    const DepthOfFieldSettings& dof,
    const LensModel& lens,
    const std::optional<raster::BBox>& clip) {
    std::vector<float> depth_vec(depth.begin(), depth.end());
    renderer::apply_per_pixel_dof(framebuffer, depth_vec, dof, lens, clip);
}

// ── draw_text_run (06 R3b wire-through — routes to renderer::draw_text_run) ─
//
// This method forwards into the canonical Blend2D text-run pipeline via
// `renderer::draw_text_run` (see `text_run_processor.hpp`).  The required
// `SoftwareProcessorContext` service bundle is sourced from `m_owner`
// (the orchestrating SoftwareRenderer) using the canonical
// `make_processor_context(SoftwareRenderer*)` helper.
//
// Failure modes:
//   - `UnsupportedCapability`: Blend2D not enabled at compile time.
//   - `InvalidInput` / `ExecutionFailure`: propagated from the renderer
//     (= defects in shape, font, or context).  Loud by design — no silent
//     zero-glyph renders.
//
// Boundary gate: this method's transient `SoftwareRenderer*` back-pointer
// is a PR-9-compatible TEMPORARY bridge.  R3 (06-stabilization) will
// source the context directly from `runtime::RenderRuntime` and drop
// `m_owner`.
graph::RenderOpResult SoftwareBackend::draw_text_run(
    Framebuffer& fb,
    const TextRunShape& shape,
    const Mat4& model_matrix,
    float opacity
) {
#ifndef CHRONON3D_USE_BLEND2D
    (void)fb; (void)shape; (void)model_matrix; (void)opacity;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "SoftwareBackend::draw_text_run: Blend2D not enabled at compile time "
        "(capabilities().text_run gated on CHRONON3D_USE_BLEND2D; see "
        "software_backend.cpp::capabilities)."
    });
#else
    CHRONON_ZONE_C("backend_draw_text_run", trace_category::kText);

    // Defensive loud-fail — without a renderer context, we cannot build a
    // SoftwareProcessorContext, and silently rendering zero glyphs is
    // exactly the silent loud-fail this wire-through is meant to fix.
    if (m_owner == nullptr) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "SoftwareBackend::draw_text_run: owner SoftwareRenderer* is null "
            "(construction contract violation).  Pass a non-null owner from "
            "RenderEngine via std::make_unique<SoftwareBackend>(...)."
        });
    }

    renderer::TextRunDrawParams params{fb, shape, model_matrix, opacity};
    return renderer::draw_text_run(make_processor_context(m_owner), params);
#endif
}

} // namespace chronon3d
