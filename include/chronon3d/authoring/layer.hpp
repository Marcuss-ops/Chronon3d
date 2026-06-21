// ═══════════════════════════════════════════════════════════════════════════
// Layer — thin authoring handle over a `LayerBuilder`.
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
// reads it to compute viewport center.  When omitted, defaults to
// 1920×1080 (matches `LayerBuilder::m_screen_width / _height` defaults).
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/extension/extension_context.hpp>  // PR 3.5 — needed to read style_registry/motion_registry pointers from the host-side ExtensionContext.
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
    /// Use this for fields the fluent surface doesn't expose yet (legacy
    /// `LayerBuilder::with_shadow`, `LayerBuilder::card3d_material`, etc.).
    /// Inlined by the compiler, no `std::function` overhead.
    template <class Fn>
    Layer& configure_core(Fn&& mutator) & {
        mutator(*builder_);
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
