// ═══════════════════════════════════════════════════════════════════════════
// Layer — thin authoring handle over a `LayerBuilder`.
//
// B1 — canonical authoring surface.  LayerBuilder is now an internal
// compilation target; the public API is `authoring::Layer`.  All methods
// delegate directly to the underlying LayerBuilder via `builder_->`.
//
// Mirrors `Text`'s design: `Layer` is a non-owning pointer to a
// `LayerBuilder` already owned by the caller.  `text(...)` returns a
// `Text` handle that mutates the new pending text-run in place.
//
// The lifetime invariant is identical to `Text`: as long as the
// `LayerBuilder` referenced by `Layer` stays alive, the `Text` handles
// returned by `text(...)` remain valid (the pending entries live in
// unique_ptr inside `LayerBuilder::m_text_runs` so push_back cannot
// invalidate them).
//
// ── Why a separate handle instead of just exposing text() on LayerBuilder?
//
// The design goal is "zero commit on destruction".  LayerBuilder's chain
// already does that implicitly (each setter returns `LayerBuilder&` for
// chaining) — but a Text handle on Layer creates a different shape:
//   `layer.text("H").font(...).animate(...)` — the `.font()` here mutates
//   directly, no commit step, and the handle can be discarded mid-chain
//   without losing state, because the underlying PendingTextRun is owned
//   by the parent LayerBuilder.
//
// ── FrameContext ─────────────────────────────────────────────────────────
//
// `Layer` accepts a `FrameContext` at construction.  `Text::center()`
// reads it to compute viewport center.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/node_handle.hpp>    // B1 — NodeHandle return from rect()
#include <chronon3d/extension/extension_context.hpp>  // PR 3.5 — needed to read style_registry/motion_registry pointers from the host-side ExtensionContext.
#include <chronon3d/text/font_engine.hpp>             // FontEngine — transitive via layer_builder.hpp -> text_run_builder.hpp; explicit here so the surface can document `FontEngine` in the doc-comment without an include-graph lookup
#include <chronon3d/authoring/text.hpp>

#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::authoring {

class Layer {
public:
    /// Primary constructor — caller-supplied `LayerBuilder` + `FrameContext`.
    /// Pass the parent's real screen_dimensions via FrameContext::from_dimensions(...)
    /// when known; otherwise default viewport is used for `.center()` resolution.
    Layer(LayerBuilder& builder, FrameContext context) noexcept
        : builder_(&builder), context_(std::move(context)) {}

    /// Façade-only constructor with viewport auto-detection.
    ///
    /// PR 4 — replaces the previous silent-default overload that picked
    /// 1920×1080 unconditionally.  Now requires the parent `LayerBuilder`
    /// to have called `screen_dimensions(w, h)` at least once before this
    /// ctor fires.
    ///
    /// ── Failure semantics ──────────────────────────────────────────────────
    /// Unconditionally throws `std::runtime_error` (no debug-mode assert
    /// short-circuit) when the parent `LayerBuilder` never called
    /// `screen_dimensions(w, h)`.  The throw site names the offending layer
    /// + the two remediation paths so the failure is self-explanatory in
    /// both interactive debuggers and CI logs.
    ///
    /// Throwing — rather than `assert(false)` + SIGABRT — keeps test
    /// runners alive (REQUIRE_THROWS_AS catches the exception cleanly)
    /// and ensures release builds see the same error signal as debug.
    ///
    /// ── Why this is no longer a silent default ─────────────────────────────
    /// `Text::center()` reads `context_->width / height` to compute viewport
    /// math.  Silently picking 1920×1080 was a footgun: a composition
    /// rendered at 1280×720 would still author text at the 1920×1080
    /// viewport, producing visually-misaligned frames.  The strict ctor
    /// surfaces the misuse at construction time instead of at render time.
    explicit Layer(LayerBuilder& builder) noexcept(false)
        : builder_(&builder),
          context_(resolve_viewport_or_throw_(builder)) {}

    Layer(const Layer&)            = delete;
    Layer& operator=(const Layer&) = delete;
    Layer(Layer&&)                 = default;
    Layer& operator=(Layer&&)      = default;

    /// Push a new text-run entry into the parent layer and return a
    /// `Text` handle that mutates the new pending TextRunSpec in place.
    ///
    /// The new entry's `name` is auto-generated as `text_<N>` where N is
    /// the per-Layer instance counter.  Multiple `.text(...)` calls on the
    /// same Layer produce independent entries.
    Text text(std::string content) {
        const std::string generated_name =
            "text_" + std::to_string(next_text_index_++);

        // Push the empty pending entry first.  Materialized when
        // LayerBuilder::build() runs.
        TextRunParams seed_spec{};
        seed_spec.text.content.value = std::move(content);

        TextRunBuilder& builder = builder_->text_run(
            generated_name,
            std::move(seed_spec)
        );

        // Public PR 3 accessor on TextRunBuilder — single Core Surface
        // extension.  The returned reference is non-owning and stable
        // (the spec itself lives behind a `unique_ptr` in
        // `LayerBuilder::m_text_runs`).
        PendingTextRun& pending = builder.mutable_pending();

        // ── PR 3.5 — pin ambient registries from ExtensionContext ────
        //
        // When the host has attached an ExtensionContext to the parent
        // LayerBuilder (via `lb.extension_context(ctx)`), the Text
        // handle returned here resolves `.style(id)` / `.motion(id)`
        // ambient against the pinned pointers. When null, the ambient
        // variants no-op gracefully and the user can supply an
        // explicit registry as a method argument.
        const chronon3d::ExtensionContext* ext = builder_->extension_context();
        const StyleRegistry* sr = (ext && ext->style_registry   != nullptr) ? ext->style_registry   : nullptr;
        const MotionRegistry* mr = (ext && ext->motion_registry != nullptr) ? ext->motion_registry : nullptr;

        return Text{pending, &context_, sr, mr};
    }

    /// Escape hatch: pass a lambda that mutates the underlying LayerBuilder.
    /// Use this for fields the fluent surface doesn't expose yet.
    ///
    /// B1 — [[deprecated]]: the canonical authoring surface now covers
    /// transforms, timing, effects, masks, blend modes, shapes, and 3D.
    /// Prefer the named methods on this class directly.
    /// Inlined by the compiler, no `std::function` overhead.
    template <class Fn>
    [[deprecated("Use the named methods on authoring::Layer instead of configure_core()")]]
    Layer& configure_core(Fn&& mutator) & {
        mutator(*builder_);
        return *this;
    }

    // ── B1 — Layer-level transforms (delegate to LayerBuilder) ─────────

    Layer& position(Vec3 p)  { builder_->position(p);  return *this; }
    Layer& scale(Vec3 s)     { builder_->scale(s);     return *this; }
    Layer& rotate(Vec3 euler_deg) { builder_->rotate(euler_deg); return *this; }
    Layer& anchor(Vec3 a)    { builder_->anchor(a);    return *this; }
    Layer& opacity(f32 v)    { builder_->opacity(v);   return *this; }

    // ── B1 — Timing (delegate to LayerBuilder) ─────────────────────────

    Layer& from(Frame f)    { builder_->from(f);    return *this; }
    Layer& duration(Frame f){ builder_->duration(f);return *this; }
    Layer& start_at(Frame f){ builder_->start_at(f);return *this; }
    Layer& length(Frame f)  { builder_->length(f);  return *this; }
    Layer& until(Frame f)   { builder_->until(f);   return *this; }
    Layer& offset(Frame f)  { builder_->offset(f);  return *this; }

    // ── B1 — Time Remap (delegate to LayerBuilder) ─────────────────────

    Layer& speed(f32 m)         { builder_->speed(m);         return *this; }
    Layer& reverse(bool v=true) { builder_->reverse(v);       return *this; }
    Layer& freeze_frame(Frame f){ builder_->freeze_frame(f);  return *this; }

    // ── B1 — Layout (delegate to LayerBuilder) ─────────────────────────

    Layer& pin_to(Anchor a, f32 margin = 0.0f) {
        builder_->pin_to(a, margin);
        return *this;
    }
    Layer& pin_to(AnchorPlacement placement, f32 margin = 0.0f) {
        builder_->pin_to(placement, margin);
        return *this;
    }

    // ── B1 — Effects (delegate to LayerBuilder) ────────────────────────

    Layer& blur(f32 radius)   { builder_->blur(radius);   return *this; }
    Layer& tint(Color color, f32 amount = 1.0f) { builder_->tint(color, amount); return *this; }
    Layer& brightness(f32 v)  { builder_->brightness(v);  return *this; }
    Layer& contrast(f32 v)    { builder_->contrast(v);    return *this; }
    Layer& saturation(f32 v)  { builder_->saturation(v);  return *this; }
    Layer& vignette(f32 radius = 0.5f, f32 softness = 0.5f, f32 amount = 1.0f) {
        builder_->vignette(radius, softness, amount);
        return *this;
    }
    Layer& drop_shadow(Vec2 offset, Color color = {0,0,0,0.35f}, f32 radius = 12.0f) {
        builder_->drop_shadow(offset, color, radius);
        return *this;
    }

    // ── B1 — Flags (delegate to LayerBuilder) ──────────────────────────

    Layer& visible(bool v)        { builder_->visible(v);      return *this; }
    Layer& kind(LayerKind k)      { builder_->kind(k);         return *this; }
    Layer& cache_static(bool v = true) { builder_->cache_static(v); return *this; }
    Layer& enable_3d(bool v=true) { builder_->enable_3d(v);    return *this; }

    // ── B1 — Blend mode (delegate to LayerBuilder) ─────────────────────

    Layer& blend(BlendMode mode) { builder_->blend(mode); return *this; }

    // ── B1 — Masks (delegate to LayerBuilder) ──────────────────────────

    Layer& mask_rect(RectMaskParams p) { builder_->mask_rect(std::move(p)); return *this; }
    Layer& mask_circle(CircleMaskParams p) { builder_->mask_circle(std::move(p)); return *this; }

    // ── B1 — Shape creation — returns NodeHandle for explicit transform access ──

    /// Create a rectangle shape and return a NodeHandle for explicit
    /// per-node transform chaining (A4 pattern).
    [[nodiscard]] NodeHandle rect(std::string name, RectParams p) {
        builder_->rect(std::move(name), std::move(p));
        return builder_->last_node_handle();
    }

    /// Create a circle shape and return a NodeHandle.
    [[nodiscard]] NodeHandle circle(std::string name, CircleParams p) {
        builder_->circle(std::move(name), std::move(p));
        return builder_->last_node_handle();
    }

    /// Create an image shape and return a NodeHandle.
    [[nodiscard]] NodeHandle image(std::string name, ImageParams p) {
        builder_->image(std::move(name), std::move(p));
        return builder_->last_node_handle();
    }

    /// Create a rounded rectangle and return a NodeHandle.
    [[nodiscard]] NodeHandle rounded_rect(std::string name, RoundedRectParams p) {
        builder_->rounded_rect(std::move(name), std::move(p));
        return builder_->last_node_handle();
    }

    // ── WP-8 PR 8.2 — per-layer FontEngine default ────────────────────
    // Forwards to `LayerBuilder::font_engine(FontEngine*)`, mirroring the
    // authoring `Text::font_engine(*)` per-spec override (defined upstream
    // in `include/chronon3d/authoring/text.hpp`).  Resolution order at
    // materialization (see `src/scene/builders/layer_builder.cpp:397` —
    // `spec.font_engine ? spec.font_engine : m_font_engine`):
    //   1. TextRunSpec/PendingTextRun.font_engine bound via Text::font_engine(...)
    //   2. LayerBuilder.m_font_engine bound here.
    //   3. The owning SoftwareRenderer's `renderer.font_engine()` (built
    //      from `runtime().resolver()`) at the draw site.
    // Setting nullptr here clears the layer default and falls back to the
    // renderer-level engine for every text node in the layer.
    Layer& font_engine(FontEngine* engine) {
        builder_->font_engine(engine);
        return *this;
    }

    /// Read-only accessor — used by tests and tooling to verify the
    /// underlying LayerBuilder state.
    [[nodiscard]] LayerBuilder&       mutable_builder()       noexcept { return *builder_; }
    [[nodiscard]] const LayerBuilder& builder()         const noexcept { return *builder_; }
    [[nodiscard]] const FrameContext& context()         const noexcept { return context_; }

private:
    /// Fail-fast viewport resolver for the one-arg `Layer(LayerBuilder&)`
    /// overload.  Behaviour:
    ///   1. Inspects `builder.screen_dimensions_were_set()` (set true by
    ///      `LayerBuilder::screen_dimensions(w, h)`);
    ///   2. On false → throws `std::runtime_error` naming the offending
    ///      layer + the two remediation paths (set `screen_dimensions`
    ///      on the builder, or use the explicit two-arg ctor).
    ///   3. On true → returns a `FrameContext` whose width/height match
    ///      the last `screen_dimensions(...)` call.
    static FrameContext resolve_viewport_or_throw_(LayerBuilder& builder) {
        if (!builder.screen_dimensions_were_set()) {
            // Canonical receiver pattern — direct-init into owning std::string
            // (the std::string(string_view) ctor since C++17 bridges the view).
            const std::string layer_name{builder.name()};
            const std::string msg =
                "chronon3d::authoring::Layer(LayerBuilder&): parent LayerBuilder '" +
                layer_name + "' was constructed without an explicit "
                "screen_dimensions(...) call. Use one of:\n"
                "  1. Call `builder.screen_dimensions(w, h)` before constructing the Layer.\n"
                "  2. Use the explicit `Layer(LayerBuilder&, FrameContext)` ctor with a viewport.\n"
                "Silently defaulting to 1920x1080 was a render-time footgun.";
            // PR 4 (per reviewer feedback): bubble the failure up as a
            // std::runtime_error unconditionally — keep test runners alive
            // (REQUIRE_THROWS_AS works), and release builds get the same
            // signal as debug without an `assert()` SIGABRT short-circuit.
            throw std::runtime_error(msg);
        }
        const Vec2 dims = builder.screen_dimensions();
        return FrameContext::from_dimensions(dims.x, dims.y);
    }

    LayerBuilder* builder_;
    FrameContext  context_;
    std::size_t   next_text_index_{0};
};

} // namespace chronon3d::authoring
