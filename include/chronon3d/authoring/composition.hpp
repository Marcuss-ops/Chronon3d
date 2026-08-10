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
//   CompositionSpec is a 5-field struct (`name`, `width`, `height`,
//   `frame_rate`, `duration`).  A fluent chain reads more
//   naturally than `composition({.name=..., .width=...}, fn)`, matches
//   the PR 1/2/3/5 design philosophy ("single `&` chain, no commit"),
//   and keeps a natural place for future surface growth
//   (`.extension_context(ctx)`, etc.).
//
// ── Closure signature (CompositionBuilder::scene) ─────────────────────
//    //   `fn(Scene&, const chronon3d::FrameContext&)` — Scene is the
    //   authoring facade (PR 4); FrameContext is the engine context with
    //   frame index / sub-frame time / duration / frame_rate fields.
    //   Two arguments, distinct types — the static_assert
//   `std::is_invocable_v<Fn, Scene&, const FrameContext&>` guards the
//   closure signature BEFORE the lambda capture happens.
//
// ── Lifetime model — the internal builder is scoped to evaluation ───
//
//   Each evaluation receives an explicit FrameContext and builds through the
//   canonical typed authoring surface inside the render-fn closure.
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
//    //   Builder surface is intentionally narrow:
    //     • spec setters: name / width / height / duration / frame_rate
    //       (one per CompositionSpec field).
//     • .scene(fn) render-fn setter (one lambda per composition).
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
        render_fn_ = [user_fn = std::forward<Fn>(fn)]
            (const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            SceneBuilder builder(ctx);
            Scene scene_handle(builder, ctx);
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
        render_fn_ = [user_fn = std::forward<Fn>(fn)]
            (const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            SceneBuilder builder(ctx);
            Scene scene_handle(builder, ctx);
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

    // Owned-by-value render function.
    std::function<chronon3d::Scene(const chronon3d::FrameContext&)> render_fn_{};
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
