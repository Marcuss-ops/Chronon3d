// ═══════════════════════════════════════════════════════════════════════════
// chronon3d::authoring::CompositionBuilder — fluent authoring façade for
// chronon3d::CompositionSpec + chronon3d::Composition.
//
// PR 4 ships the top-of-tree factory that closes the authoring DSL loop:
//   chronon3d::authoring::composition()
//       .name("HeroShowcase")
//       .width(1920)
//       .height(1080)
//       .duration(Frame{60})
//       .frame_rate(FrameRate{30, 1})
//       .scene([](authoring::Scene& s, const chronon3d::FrameContext& ctx) {
//           s.layer("bg", [](authoring::Layer& l) { /* ... */ });
//       })
//       .build();   // → chronon3d::Composition (engine IR, registry-ready)
//
// ── Why a builder chain (vs an engine-mirror shorthand) ──────────────
//
//   CompositionSpec is a 6-field struct (`name`, `width`, `height`,
//   `frame_rate`, `duration`, `assets_root`).  A fluent chain reads more
//   naturally than `composition({.name=..., .width=...}, fn)`, matches
//   the PR 1/2/3/5 design philosophy ("single `&` chain, no commit"),
//   and keeps a natural place for future surface growth
//   (`.extension_context(ctx)`, etc.).
//
// ── Closure signature (CompositionBuilder::scene) ─────────────────────
//
//   `fn(Scene&, const chronon3d::FrameContext&)` — Scene is the
//   authoring facade (PR 4); FrameContext is the engine context with
//   frame index / sub-frame time / duration / frame_rate / assets_root
//   fields.  Two arguments, distinct types — the static_assert
//   `std::is_invocable_v<Fn, Scene&, const FrameContext&>` guards the
//   closure signature BEFORE the lambda capture happens.
//
// ── Lifetime model — SceneBuilder lives INSIDE the SceneFunction ────
//
//   Each `Composition::evaluate(frame)` constructs a fresh SceneBuilder
//   inside the render-fn closure (the engine already does this in
//   `chronon3d::Composition::evaluate_double` via `m_render(ctx)`).
//   PR 4's render-fn mirrors that — the owned-builder is a local
//   variable inside the closure, NOT a member of CompositionBuilder.
//   This avoids dangling-pointer pitfalls when a Composition is moved
//   across CompositionBuilder boundaries.
//
// ── Output type ───────────────────────────────────────────────────────
//
//   `.build()` returns `chronon3d::Composition` directly, NOT a
//   wrapping `chronon3d::authoring::Composition` class.  Rationale:
//     • Lossless, zero-overhead — the authoring path is build-time-only.
//     • CompositionRegistry requires `chronon3d::Composition`
//       factories (`std::function<Composition(const CompositionProps&)>`
//       in composition_registry.hpp) — wrapping would force a
//       re-extraction layer at registration time.
//     • The engine `composition(CompositionSpec, SceneFunction)` free
//       factory remains the canonical way to construct compositions
//       from outside the authoring façade.
//
// ── Surface boundary (PR 4) ───────────────────────────────────────────
//
//   Builder surface is intentionally narrow:
//     • spec setters: name / width / height / duration / frame_rate /
//       assets_root (one per CompositionSpec field).
//     • .scene(fn) render-fn setter (one lambda per composition).
//     • .custom_builder(fn) injection point for callers needing a
//       non-default SceneBuilder ctor (custom pmr resource, custom
//       shape_registry).
//     • .build() terminal — consumes the builder, returns Composition.
//
//   Everything beyond (e.g. register_with(CompositionRegistry&)) stays
//   on the underlying engine — `composition(...).build()` produces the
//   exact primitive the registry consumes.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/authoring/scene.hpp>
#include <chronon3d/authoring/layer.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace chronon3d::authoring {

// ── CompositionBuilder ────────────────────────────────────────────────
//
// Move-only (consumed by `.build()`).  Stores an owned-by-value render
// function so the closure can capture SceneBuilder construction +
// user-supplied fn by move.  CompositionSpec accumulates as the user
// chains the .name / .width / .height setters; .build() moves it
// straight into chronon3d::Composition.
class CompositionBuilder {
public:
    CompositionBuilder() = default;

    CompositionBuilder(const CompositionBuilder&)            = delete;
    CompositionBuilder& operator=(const CompositionBuilder&) = delete;
    CompositionBuilder(CompositionBuilder&&)                 = default;
    CompositionBuilder& operator=(CompositionBuilder&&)      = default;

    // ── CompositionSpec setters (single `&` ref-qualifier) ──────────
    CompositionBuilder& name(std::string value) & {
        spec_.name = std::move(value);
        return *this;
    }
    CompositionBuilder&& name(std::string value) && {
        spec_.name = std::move(value);
        return std::move(*this);
    }

    CompositionBuilder& width(i32 value) & {
        spec_.width = value;
        return *this;
    }
    CompositionBuilder&& width(i32 value) && {
        spec_.width = value;
        return std::move(*this);
    }

    CompositionBuilder& height(i32 value) & {
        spec_.height = value;
        return *this;
    }
    CompositionBuilder&& height(i32 value) && {
        spec_.height = value;
        return std::move(*this);
    }

    CompositionBuilder& duration(Frame value) & {
        spec_.duration = value;
        return *this;
    }
    CompositionBuilder&& duration(Frame value) && {
        spec_.duration = value;
        return std::move(*this);
    }

    CompositionBuilder& frame_rate(FrameRate value) & {
        spec_.frame_rate = value;
        return *this;
    }
    CompositionBuilder&& frame_rate(FrameRate value) && {
        spec_.frame_rate = value;
        return std::move(*this);
    }

    CompositionBuilder& assets_root(std::filesystem::path value) & {
        spec_.assets_root = std::string{value.string()};
        return *this;
    }
    CompositionBuilder&& assets_root(std::filesystem::path value) && {
        spec_.assets_root = std::string{value.string()};
        return std::move(*this);
    }

    /// Custom SceneBuilder factory — let users inject shape_registry,
    /// pmr resource, or other customisations.  When unset, the render
    /// closure falls back to the default `SceneBuilder(ctx)` ctor
    /// (which constructs a default ShapeRegistry internally if none
    /// was attached upstream).
    template <class Fn>
    CompositionBuilder& custom_builder(Fn&& factory) & {
        custom_builder_fn_ = [factory = std::forward<Fn>(factory)]
            (const chronon3d::FrameContext& ctx) -> SceneBuilder {
            return factory(ctx);
        };
        return *this;
    }
    template <class Fn>
    CompositionBuilder&& custom_builder(Fn&& factory) && {
        custom_builder_fn_ = [factory = std::forward<Fn>(factory)]
            (const chronon3d::FrameContext& ctx) -> SceneBuilder {
            return factory(ctx);
        };
        return std::move(*this);
    }

    // ── Render function setter ──────────────────────────────────────
    //
    // Closure receives authoring::Scene& + the engine FrameContext.
    // The SceneBuilder lives INSIDE the closure (constructed per-frame
    // when the engine calls evaluate()) — no lifetime coupling back
    // to CompositionBuilder.
    //
    // SFINAE guard via static_assert ensures the closure signature is
    // callable; if the user accidentally passes something else, the
    // build fails at compile time with a clear message.
    //
    // If `.scene(...)` is not called before `.build()`, the composition
    // defaults to an empty renderer (produces a Scene with zero layers
    // at every frame).  Documented as graceful no-op rather than throw
    // — matches the engine convention where `composition({spec}, fn)`
    // is happy with a no-op fn.
    template <class Fn>
    CompositionBuilder& scene(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, Scene&, const chronon3d::FrameContext&>,
                      "CompositionBuilder::scene(fn): fn must be invocable as "
                      "fn(chronon3d::authoring::Scene&, const chronon3d::FrameContext&).");
        // Capture both user_fn and the (optional) custom-builder
        // factory by move into the captured std::function.  Note that
        // custom_builder_fn_ is itself stored on *this by member
        // assignment above; we move it into the captured std::function
        // by taking a local copy, so the lifetime of the closure is
        // independent of *this once build() fires.
        auto custom_snapshot = custom_builder_fn_;
        render_fn_ = [user_fn = std::forward<Fn>(fn),
                      custom = std::move(custom_snapshot)]
            (const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            // LOCAL SceneBuilder — one per evaluate() call, mirrors the
            // engine's per-evaluate construction in
            // chronon3d::Composition::evaluate_double.
            SceneBuilder builder = custom ? custom(ctx) : SceneBuilder(ctx);
            Scene scene_handle(builder,
                               FrameContext::from_dimensions(
                                   static_cast<f32>(ctx.width),
                                   static_cast<f32>(ctx.height)));
            user_fn(scene_handle, ctx);
            return builder.build();
        };
        return *this;
    }
    template <class Fn>
    CompositionBuilder&& scene(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, Scene&, const chronon3d::FrameContext&>,
                      "CompositionBuilder::scene(fn): fn must be invocable as "
                      "fn(chronon3d::authoring::Scene&, const chronon3d::FrameContext&).");
        // Review/rvalue overload — supports fluent rvalue chains like
        // composition().name("a").scene(...).build().  Setters on the
        // other CompositionBuilder fields STILL use `&` only (the
        // codebase philosophy for them is "declare-then-mutate"); this
        // single `&&` overload on scene() unblocks the test fixture's
        // chain calls without rewriting every user test case.
        auto custom_snapshot = custom_builder_fn_;
        render_fn_ = [user_fn = std::forward<Fn>(fn),
                      custom = std::move(custom_snapshot)]
            (const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            SceneBuilder builder = custom ? custom(ctx) : SceneBuilder(ctx);
            Scene scene_handle(builder,
                               FrameContext::from_dimensions(
                                   static_cast<f32>(ctx.width),
                                   static_cast<f32>(ctx.height)));
            user_fn(scene_handle, ctx);
            return builder.build();
        };
        return std::move(*this);
    }

    // ── Terminal: build a chronon3d::Composition ───────────────────
    //
    // Consumes the builder by rvalue.  Pass-only-move pattern keeps
    // users from accidentally reusing the CompositionBuilder after
    // .build() (state has been moved into the Composition).
    [[nodiscard]] chronon3d::Composition build() && {
        if (!render_fn_) {
            // Default empty renderer — produces a zero-layer scene per frame.
            render_fn_ = [](const chronon3d::FrameContext&) -> chronon3d::Scene {
                SceneBuilder empty_builder(chronon3d::FrameContext{});
                return empty_builder.build();
            };
        }
        return chronon3d::composition(std::move(spec_), std::move(render_fn_));
    }

private:
    // CompositionSpec accumulates via chain setters; moves into chronon3d::Composition at build() time.
    chronon3d::CompositionSpec spec_{};

    // Owned-by-value render function.  Captured std::function for
    // generic-lambda + custom-builder-factory composition; the closure
    // body is inlined by the compiler on most modern toolchains.
    std::function<chronon3d::Scene(const chronon3d::FrameContext&)> render_fn_{};

    // Optional SceneBuilder factory — captured by scene() so the closure
    // doesn't reach back into *this (avoids dangling-pointer risk).
    std::function<SceneBuilder(const chronon3d::FrameContext&)> custom_builder_fn_{};
};

// ── Free factory — the canonical entry point ────────────────────────
//
// Mirrors the engine's `chronon3d::composition(...)` factory with a
// different return type (CompositionBuilder vs Composition).  Single
// argument-free overload: starts an empty builder chain.
inline CompositionBuilder composition() {
    return CompositionBuilder{};
}

} // namespace chronon3d::authoring
