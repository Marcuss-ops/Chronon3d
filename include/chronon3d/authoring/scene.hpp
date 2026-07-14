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
// ── Closure signature dispatch (SFINAE) ───────────────────────────────
//
// `Scene::layer("name", fn)` accepts a closure with EITHER signature:
//   (a) `fn(LayerBuilder&) -> void`  — engine / raw API style, passthrough
//   (b) `fn(Layer&) -> void`        — PR 3 authoring facade (recommended)
//
// The dispatch is `if constexpr` — the compiler instantiates only the
// matching branch.  No `std::function` overhead; the wrapper lambda
// captures the user fn by move.
//
// ── FrameContext plumbing ─────────────────────────────────────────────
//
// The handle stores an `authoring::FrameContext` so it can pass viewport
// info to Layer → Text.  This is the same shape used in PR 3
// (`Layer(LayerBuilder&, FrameContext)`).  Width + height are normalised
// to f32; sub-frame time / duration / assets-root are intentionally
// excluded — they live on the engine FrameContext propagated by the
// composition render function, not on the authoring handle.
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
// edit_camera) is reachable via `.configure_core([&](SceneBuilder&
// core){ ... })` — the Level-3 escape hatch consistent with PR 3
// Layer.  Future PRs may expose more verbs once a clear demand pattern
// emerges; the surface ships narrow on purpose.
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
    /// Primary constructor — caller supplies a SceneBuilder (lifetime
    /// owned by the composition's render-fn closure) plus the viewport
    /// FrameContext used by Layer → Text resolution.  The SceneBuilder
    /// should be constructed with a matching `FrameContext` (matching
    /// width/height) so layer positions resolve as expected.
    Scene(SceneBuilder& builder, FrameContext context) noexcept
        : builder_(&builder), context_(std::move(context)) {}

    /// Convenience overload — default 1920×1080 viewport, used when the
    /// composition's FrameContext isn't known at Scene construction.
    /// Prefer the explicit overload in production code.
    ///
    /// A2 — deprecated: silently assumes 1920×1080, which produces
    /// misaligned text in non-16:9 compositions.  Use the two-arg ctor
    /// Scene(builder, FrameContext::from_dimensions(w, h)) explicitly.
    [[deprecated("Use Scene(builder, FrameContext::from_dimensions(w, h)) with explicit dimensions")]]
    explicit Scene(SceneBuilder& builder) noexcept
        : Scene(builder, FrameContext::default_viewport()) {}

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&)                 = default;
    Scene& operator=(Scene&&)      = default;

    // ── Layer dispatch (SFINAE dual-surface) ────────────────────────────
    //
    // Branch (a) — closure takes authoring::Layer& (PR 3 surface).  We
    // wrap the SceneBuilder-spawned LayerBuilder as Layer so the user
    // can chain PR 3 surface methods (l.text(...), l.configure_core(...)).
    // Branch (b) — closure takes raw LayerBuilder&; passthrough.
    template <class Fn>
    Scene& layer(std::string name, Fn&& fn) & {
        using NakedFn = std::remove_cv_t<std::remove_reference_t<Fn>>;
        if constexpr (std::is_invocable_v<NakedFn, Layer&>) {
            builder_->layer(std::move(name), [this, fn = std::forward<Fn>(fn)](LayerBuilder& lb) {
                Layer layer_handle(lb, context_);
                fn(layer_handle);
            });
        } else {
            // Passthrough — user wants raw LayerBuilder (engine surface).
            builder_->layer(std::move(name), std::forward<Fn>(fn));
        }
        return *this;
    }

    /// B2.2 — `Sequence` forwarder.  Mirrors `SceneBuilder::sequence`'s
    /// dual-surface contract: the closure may take either
    /// `SequenceBuilder&` (PR 4 authoring facade, recommended — gives
    /// the lambda access to `local_frame()` / `duration()` / `progress()`
    /// context accessors + a sequenced layer API) or `SceneBuilder&`
    /// (engine surface, passthrough, equivalent to the pre-PR-4 path).
    ///
    /// Internal dispatch (the `if constexpr` inside `compile_sequence()`
    /// at `detail/scene_builder_sequences.inl`) auto-detects the closure
    /// surface and constructs the appropriate builder; this wrapper
    /// simply forwards the call unchanged and lets the compile-sequence
    /// plumbing own the routing.
    ///
    /// Example (forward-point audit):
    /// ```cpp
    /// scene.sequence("intro",
    ///                { .from = Frame{0}, .duration = Frame{60} },
    ///                [](SequenceBuilder& seq) {
    ///                    seq.layer("title", [](LayerBuilder& l) { /* ... */ });
    ///                });
    /// ```
    ///
    /// Scope: B2.2 — Refactor-block-2 thin forwarder.  No new timeline
    /// compiler, no override of `compile_sequence()`.  Delegates verbatim
    /// to `SceneBuilder::sequence(name, spec, fn)` which — per the canonical
    /// SSoT recorded in `tools/check_single_source_of_truth.sh` Concept 8
    /// — ultimately invokes the single canonical `compile_sequence()`.
    template <class Fn>
    Scene& sequence(std::string name,
                    SceneBuilder::SequenceSpec spec,
                    Fn&& fn) & {
        using NakedFn = std::remove_cv_t<std::remove_reference_t<Fn>>;
        static_assert(
            std::is_invocable_v<NakedFn, SequenceBuilder&>
         || std::is_invocable_v<NakedFn, SceneBuilder&>,
            "Scene::sequence(): closure must take SequenceBuilder& or SceneBuilder&");
        builder_->sequence(std::move(name),
                           std::move(spec),
                           std::forward<Fn>(fn));
        return *this;
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

    /// B2.3 — `screen_layer` forwarder with dual-surface SFINAE
    /// dispatch (mirrors `Scene::layer` Surface-A / Surface-B
    /// contract):
    ///   (a) `fn(Layer&)`         — PR 3 authoring facade (recommended)
    ///   (b) `fn(LayerBuilder&)`  — engine passthrough
    /// No new timeline / no override of any underlying engine method.
    /// The dispatch is `if constexpr` — the compiler instantiates only
    /// the matching branch.
    template <class Fn>
    Scene& screen_layer(std::string name, Fn&& fn) & {
        using NakedFn = std::remove_cv_t<std::remove_reference_t<Fn>>;
        if constexpr (std::is_invocable_v<NakedFn, Layer&>) {
            builder_->screen_layer(std::move(name),
                [this, fn = std::forward<Fn>(fn)](LayerBuilder& lb) {
                    Layer layer_handle(lb, context_);
                    fn(layer_handle);
                });
        } else {
            builder_->screen_layer(std::move(name), std::forward<Fn>(fn));
        }
        return *this;
    }

    /// B2.3 — `precomp_layer` forwarder with dual-surface SFINAE
    /// dispatch (mirrors `Scene::layer` Surface-A / Surface-B
    /// contract).  The underlying `SceneBuilder::precomp_layer(name,
    /// comp_name, fn)` references a named precomp composition; this
    /// wrapper preserves that exact arity.
    template <class Fn>
    Scene& precomp(std::string name, std::string comp_name, Fn&& fn) & {
        using NakedFn = std::remove_cv_t<std::remove_reference_t<Fn>>;
        if constexpr (std::is_invocable_v<NakedFn, Layer&>) {
            builder_->precomp_layer(std::move(name), std::move(comp_name),
                [this, fn = std::forward<Fn>(fn)](LayerBuilder& lb) {
                    Layer layer_handle(lb, context_);
                    fn(layer_handle);
                });
        } else {
            builder_->precomp_layer(std::move(name), std::move(comp_name),
                                    std::forward<Fn>(fn));
        }
        return *this;
    }

    // ── Escape hatch ────────────────────────────────────────────────────
    /// Pass a lambda that mutates the underlying SceneBuilder.  Use for
    /// fields the fluent surface doesn't expose yet (camera wiring,
    /// lighting rig, stagger, sequence, etc.).  Inlined by the compiler,
    /// no `std::function` overhead.
    ///
    /// Single `&` ref-qualifier matches the convention used by Layer /
    /// Text / Animator / Material / Selector — the codebase deliberately
    /// does NOT duplicate `&` / `&&` overload pairs.
    template <class Fn>
    Scene& configure_core(Fn&& mutator) & {
        mutator(*builder_);
        return *this;
    }

    // ── Read-only accessors (for tests and tooling) ──────────────────────
    [[nodiscard]] SceneBuilder&       mutable_builder()       noexcept { return *builder_; }
    [[nodiscard]] const SceneBuilder& builder()         const noexcept { return *builder_; }
    [[nodiscard]] const FrameContext& context()         const noexcept { return context_; }

private:
    SceneBuilder* builder_;
    FrameContext  context_;
};

} // namespace chronon3d::authoring
