// ═══════════════════════════════════════════════════════════════════════════
// chronon3d::authoring::Scene — thin handle over chronon3d::SceneBuilder.
//
// PR 4 wraps `SceneBuilder` so user code can drive the scene through the
// authoring DSL.  The design mirrors PR 3's `Layer` handle:
//   • Owning the SceneBuilder locally would force commit-on-destruction
//     semantics — instead `Scene` mutates the single source of truth
//     already owned by the parent composition, and the caller's
//     `Scene::Scene(...)` lifecycle is symmetric with `Layer::Layer(...)`.
//   • Single `&` ref-qualifier per setter, identical user-facing syntax
//     to Layer / Text / Animator / Material / Selector.
//
// Public callbacks use the typed authoring handles. Builder objects remain
// implementation details of the facade.
//
// ── Canonical context plumbing ────────────────────────────────────────
//
// Rendering owns the single `chronon3d::FrameContext`. Scene converts its
// width/height exactly once through `CanvasInfo::with_safe_area(...)`, then
// carries CanvasInfo through Layer → Text placement. There is no separate
// authoring FrameContext and no implicit 1920×1080 Scene constructor.
//
// ── Surface boundary (PR 4 + B2.2 + B2.3) ──────────────────────────────
//
// Verbs exposed by the Authoring facade:
//   * `.layer(...)`                — PR 4
//   * `.sequence(name, spec, ...)` — B2.2
//   * `.camera()`                  — B2.3 (returns CameraApi)
//   * `.background(name, p)`       — B2.3
//   * `.screen_layer(name, ...)`   — B2.3
//   * `.precomp(name, comp, ...)`  — B2.3
//   * `.image(name, p)`            — B2.3
// Everything else (stagger / apply_lighting_rig / shape primitives /
// edit_camera) remains on the internal SceneBuilder surface until it gets
// an explicit, typed authoring facade.  The public facade has no raw-builder
// escape hatch.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/authoring/layer.hpp>

#include <string>
#include <type_traits>
#include <utility>

namespace chronon3d::authoring {

class Scene {
public:
    /// Primary authoring constructor for callers that already have a custom
    /// safe-area descriptor.
    Scene(SceneBuilder& builder, CanvasInfo canvas) noexcept
        : builder_(&builder), canvas_(std::move(canvas)) {}

    /// Canonical render-to-authoring bridge:
    /// chronon3d::FrameContext → CanvasInfo::with_safe_area(...).
    Scene(SceneBuilder& builder, const chronon3d::FrameContext& frame_context)
        : Scene(builder,
                CanvasInfo::with_safe_area(
                    static_cast<f32>(frame_context.width),
                    static_cast<f32>(frame_context.height),
                    SafeAreaPreset{})) {}

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&)                 = default;
    Scene& operator=(Scene&&)      = default;

    // ── Typed layer surface ────────────────────────────────────────────
    template <class Fn>
    Scene& layer(std::string name, Fn&& fn) & {
        using NakedFn = std::remove_cv_t<std::remove_reference_t<Fn>>;
        static_assert(std::is_invocable_v<NakedFn, Layer&>,
                      "Scene::layer(): closure must take authoring::Layer&");
        builder_->layer(std::move(name), [this, fn = std::forward<Fn>(fn)](LayerBuilder& lb) {
            Layer layer_handle(lb, canvas_);
            fn(layer_handle);
        });
        return *this;
    }

    /// B2.2 — `Sequence` forwarder. The callback receives the canonical
    /// `SequenceBuilder` context with local-time accessors and sequenced
    /// layer creation.
    ///
    /// Example (forward-point audit):
    /// ```cpp
    /// scene.sequence("intro",
    ///                { .from = Frame{0}, .duration = Frame{60} },
    ///                [](SequenceBuilder& seq) {
    ///                    seq.layer("title", [](Layer& l) { /* ... */ });
    ///                });
    /// ```
    ///
    /// Scope: B2.2 — thin forwarder to the canonical sequence compiler.
    template <class Fn>
    Scene& sequence(std::string name,
                    SceneBuilder::SequenceSpec spec,
                    Fn&& fn) & {
        using NakedFn = std::remove_cv_t<std::remove_reference_t<Fn>>;
        static_assert(std::is_invocable_v<NakedFn, SequenceBuilder&>,
                      "Scene::sequence(): closure must take SequenceBuilder&");
        builder_->sequence(std::move(name),
                           std::move(spec),
                           std::forward<Fn>(fn));
        return *this;
    }

    /// B2.x — `Series` forwarder.  Delegates to `SceneBuilder::series(name)`
    /// and returns the resulting `SeriesBuilder` so callers can chain
    /// `.add(...)` calls.  The returned proxy references the same
    /// `SceneBuilder` owned by this facade; it must not outlive this
    /// `Scene` (and therefore the underlying `SceneBuilder`).
    [[nodiscard]] SeriesBuilder series(const std::string& name = {}) {
        return builder_->series(name);
    }

    // ── B2.3 — camera(), background(), image(), screen_layer(), precomp()
    //
    // Five thin forwarders to existing SceneBuilder surfaces.  All
    // delegates are verbatim (no transformation, no adapter) — the
    // pattern is "thin facade → existing canonical system", per
    // AGENTS.md Cat-3 anti-duplication.  Each forwarder preserves a
    // distinct surface contract documented inline below.

    /// B2.3 — `CameraApi` forwarder.  `SceneBuilder::camera()` returns a
    /// value-typed sub-builder (`CameraApi`) that the caller uses to
    /// configure the scene's camera through a fluent chain
    /// (`scene.camera().position({0,0,5}).zoom(2.0)`).  This forwarder
    /// intentionally returns `CameraApi` by value — NOT `Scene&` —
    /// mirroring the underlying SceneBuilder contract verbatim.
    /// Callers do NOT chain further Scene methods on `camera()`; the
    /// CameraApi handles its own fluent surface (`camera()` is a
    /// terminal sub-builder getter, by design).
    [[nodiscard]] CameraApi camera() {
        return builder_->camera();
    }

    /// B2.3 — `grid_background` forwarder.  Thin delegate to
    /// `SceneBuilder::grid_background(name, p)`.  Returns `Scene&` to
    /// preserve the fluent surface for chained verbs (camera, layer,
    /// sequence, …).
    Scene& background(std::string name, GridBackgroundParams p) & {
        builder_->grid_background(std::move(name), std::move(p));
        return *this;
    }

    /// B2.3 — `image` forwarder.  Thin delegate to
    /// `SceneBuilder::image(name, p)`.  Returns `Scene&` for fluent
    /// chaining.
    Scene& image(std::string name, ImageParams p) & {
        builder_->image(std::move(name), std::move(p));
        return *this;
    }

    /// B2.3 — typed `screen_layer` forwarder.
    template <class Fn>
    Scene& screen_layer(std::string name, Fn&& fn) & {
        using NakedFn = std::remove_cv_t<std::remove_reference_t<Fn>>;
        static_assert(std::is_invocable_v<NakedFn, Layer&>,
                      "Scene::screen_layer(): closure must take authoring::Layer&");
        builder_->screen_layer(std::move(name),
            [this, fn = std::forward<Fn>(fn)](LayerBuilder& lb) {
                Layer layer_handle(lb, canvas_);
                fn(layer_handle);
            });
        return *this;
    }

    /// B2.3 — typed `precomp_layer` forwarder.
    template <class Fn>
    Scene& precomp(std::string name, std::string comp_name, Fn&& fn) & {
        using NakedFn = std::remove_cv_t<std::remove_reference_t<Fn>>;
        static_assert(std::is_invocable_v<NakedFn, Layer&>,
                      "Scene::precomp(): closure must take authoring::Layer&");
        builder_->precomp_layer(std::move(name), std::move(comp_name),
            [this, fn = std::forward<Fn>(fn)](LayerBuilder& lb) {
                Layer layer_handle(lb, canvas_);
                fn(layer_handle);
            });
        return *this;
    }

    [[nodiscard]] const CanvasInfo&   canvas()          const noexcept { return canvas_; }

private:
    SceneBuilder* builder_;
    CanvasInfo    canvas_;
};

} // namespace chronon3d::authoring
