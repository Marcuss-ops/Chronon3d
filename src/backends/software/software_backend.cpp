#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/utils/effects/per_pixel_dof.hpp>
#include <chronon3d/backends/software/text_run_processor.hpp>  // 06 R3b wire-through
#include <chronon3d/backends/software/software_processor_context.hpp>
#include "internal/software_processor_services.hpp"  // TICKET-118 (PUBLIC via parent CMakeLists)
#include <chronon3d/backends/software/software_registry.hpp>    // TICKET-118: SoftwareRegistry full type for draw_node dispatch
#include <chronon3d/scene/model/render/render_node.hpp>          // TICKET-118: RenderNode full type for draw_node (node.shape.type())
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/trace_categories.hpp>
#include <chronon3d/scene/model/layer/layer_effect.hpp>
// TICKET-118 — explicit include for `spdlog::error(...)` calls in
// SoftwareBackend::draw_node loud-fail paths.  SoftwareBackend::draw_node
// emits a structured error when `m_proc_ctx` is not attached (i.e. caller
// forgot to invoke `attach_processor_context(...)` after construction);
// also emits when a non-TextRun shape is missing its processor.  Both
// paths surface in test logs so a regression is observable in CI.
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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

// ═════════════════════════════════════════════════════════════════════════════
//  Construction — single services-object ctor (TICKET-011b). Direct
//  construction skips validation; production code MUST route through
//  `make_software_backend(...)`.
// ═════════════════════════════════════════════════════════════════════════════

SoftwareBackend::SoftwareBackend(SoftwareBackendServices services)
    : m_counters(services.counters)
    , m_settings(services.settings)
    , m_framebuffer_pool(std::move(services.framebuffer_pool))   // share ownership with services
    , m_asset_resolver(services.asset_resolver)
    , m_text_resources(services.text_resources)
{
    // TICKET-118 — m_owner REMOVED.  The processor context that used to be
    // sourced via m_owner is supplied separately via
    // attach_processor_context() immediately after construction.  See
    // `internal/software_processor_services.hpp` for the derivation story.
}

SoftwareBackend::~SoftwareBackend() = default;

// ── TICKET-118 — attach_processor_context ─────────────────────────────
//
// Internal setter used by runtime_adapter.cpp + tests/helpers/test_utils.hpp
// to wire the orchestrator-supplied processor-context bundle to this
// backend.  Idempotent; later calls REPLACE the previous context.
//
// Note: this method is intentionally PUBLIC (declared in software_backend.hpp)
// because the backend crosses a TU boundary: the constructed unique_ptr
// returned by `make_software_backend()` is the object callers reach; we
// don't expose it via friend declarations.  AGENTS.md v0.1 freeze
// permits this single-method additive surface change because the
// underlying `SoftwareProcessorContext` type is already public.
void SoftwareBackend::attach_processor_context(SoftwareProcessorContext proc_ctx) {
    m_proc_ctx = proc_ctx;
}

// ── draw_node (real — TICKET-118 closure) ───────────────────────────────
//
// SoftwareBackend::draw_node is no longer a no-op stub.  The dispatch
// finds the shape processor in `m_proc_ctx.registry` and forwards the
// call with the cached processor-context bundle.
//
// For `ShapeType::TextRun` the lookup intentionally misses: text runs
// are dispatched through `TextRunNode` → `draw_text_run` directly,
// so the canonical integration does NOT route TextRun shapes through
// `draw_node`.  We return SILENTLY on a TextRun shape arriving here
// instead of emitting a "No processor registered" error (which would
// spam the per-frame log).
//
// Lifetime invariant for `m_proc_ctx.registry`: the orchestrating
// SoftwareRenderer owns the registry via `unique_ptr<SoftwareRegistry>`;
// that renderer outlives any backend created on its runtime because
// `~RenderRuntime()` runs BEFORE `~SoftwareRenderer()`.  We therefore
// read `m_proc_ctx.registry` ONLY inside dispatch (never from
// `~SoftwareBackend()`).
void SoftwareBackend::draw_node(Framebuffer& fb, const RenderNode& node,
                                 const RenderState& state,
                                 const Camera& camera, int width, int height) {
    CHRONON_ZONE_C("backend_draw_node", trace_category::kRasterize);
    if (!m_proc_ctx.registry) {
        // Defensive: caller forgot to attach_processor_context() before
        // dispatching.  Log loudly so the regression surfaces in CI.
        spdlog::error(
            "SoftwareBackend::draw_node called without an attached processor-context "
            "(call attach_processor_context() after make_software_backend).  "
            "Returning without rendering shape type={}.",
            static_cast<int>(node.shape.type()));
        return;
    }
    auto* processor = m_proc_ctx.registry->get_shape(node.shape.type());
    if (!processor) {
        if (node.shape.type() == ShapeType::TextRun) {
            // Canonical TextRun integration is via TextRunNode →
            // draw_text_run(...).  draw_node() reaching this shape
            // is a defensive no-op; silent to avoid log spam.
            return;
        }
        spdlog::error("SoftwareBackend::draw_node: no processor registered for shape type {}", static_cast<int>(node.shape.type()));
        return;
    }
    if (m_counters) {
        m_counters->pixels_touched.fetch_add(
            clipped_area(width, height, to_local_clip(fb, state.clip_rect)),
            std::memory_order_relaxed);
    }
    processor->draw(m_proc_ctx, fb, node, state, camera, width, height);
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
    m_counters->blur_pixels.fetch_add(
        clipped_area(fb.width(), fb.height(), local_clip),
        std::memory_order_relaxed);
    CHRONON_ZONE_C("apply_blur", trace_category::kEffect);
    renderer::apply_blur(fb, radius, local_clip, 2);
}

// ── composite_layer ───────────────────────────────────────────────────────

void SoftwareBackend::composite_layer(
    Framebuffer& dst, const Framebuffer& src, BlendMode mode,
    const std::optional<raster::BBox>& clip, CompositeOperator op) {
    m_counters->layers_rendered.fetch_add(1, std::memory_order_relaxed);
    CHRONON_ZONE_C("composite_layer", trace_category::kComposite);
    m_counters->pixels_touched.fetch_add(
        clipped_area(dst.width(), dst.height(), to_local_clip(dst, clip)),
        std::memory_order_relaxed);
    SoftwareCompositor::composite_layer(dst, src, mode, clip, op,
        m_settings->force_scalar_normal_blend);
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
            m_counters->blur_pixels.fetch_add(area, std::memory_order_relaxed);
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
    // Forward the incoming (non-owning) span directly to the kernel.
    // No copy: 2,073,600 floats × 4 B = 8 MiB saved per dispatch at 1920×1080.
    renderer::apply_per_pixel_dof(framebuffer, depth, dof, lens, clip);
}

// ── draw_text_run (06 R3b wire-through — routes to renderer::draw_text_run) ─
//
// This method forwards into the canonical Blend2D text-run pipeline via
// `renderer::draw_text_run` (see `text_run_processor.hpp`).  The required
// `SoftwareProcessorContext` service bundle is sourced from `m_owner`
// (the orchestrating SoftwareRenderer) using the canonical
// `make_processor_context(SoftwareRenderer*)` helper.
//
// Failure modes (post-construction contract is louder than pre-fix):
//   - `UnsupportedCapability`: Blend2D not enabled at compile time.
//   - `InvalidInput` / `ExecutionFailure`: propagated from the renderer
//     (= defects in shape, font, or context).  Loud by design — no silent
//     zero-glyph renders.
//   - Missing `m_owner`            → InvalidInput (TICKET-046 follow-up
//                                    will source from runtime, then owner
//                                    becomes null-tolerable).
//   - Missing `m_asset_resolver`   → InvalidInput (Fase 1 services-
//                                    validation follow-up).
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

    // TICKET-118 — m_owner NO MORE.  The processor context is attached
    // via attach_processor_context() immediately after construction.
    // We use m_proc_ctx.asset_resolver (mirrors services.asset_resolver)
    // as the loud-fail null-check so the load path stays defensive even
    // when attach_processor_context() was forgotten.
    if (m_asset_resolver == nullptr) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "SoftwareBackend::draw_text_run: asset_resolver is null (REQUIRED "
            "service missing — construction should be routed through "
            "make_software_backend() so the validator surfaces this at startup)."
        });
    }

    renderer::TextRunDrawParams params{fb, shape, model_matrix, opacity};
    graph::RenderOpResult result = renderer::draw_text_run(m_proc_ctx, params);

    return result;
#endif
}

} // namespace chronon3d

