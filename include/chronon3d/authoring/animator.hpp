// ═══════════════════════════════════════════════════════════════════════════
// Animator — fluent authoring façade over `TextAnimatorSpec`.
//
// Builds an `TextAnimatorSpec` incrementally so user code does not have
// to interact with `std::vector<TextAnimatorProperty>`, the variant, or
// `push_back` directly.
//
// Construction goes through the `animator(id)` factory:
//
//   auto a = animator("hero")
//                .select(selector(TextSelectorUnit::Word)
//                              .start(Frame{0},  0.0f)
//                              .start(Frame{30}, 100.0f))
//                .position({0.0f, 46.0f, 0.0f})
//                .scale(0.94f)
//                .opacity(0.0f)
//                .blur(12.0f)
//                .tracking(10.0f);
//
// The builder is move-only. Consumers pull the spec out via the rvalue
// `release()` helper (see `friend` declarations below).
//
// ── C++20 ref-qualifier note ────────────────────────────────────────────
//
// Single `&` ref-qualifier per setter instead of duplicated `&`/`&&`
// pairs — see `selector.hpp` for the rationale. Chain syntax is
// identical for users; the final consumer moves the builder out.
//
// ── Blend modes ─────────────────────────────────────────────────────────
//
// `TextAnimatorSpec` carries two independent blend modes (transform vs
// colour). The fluent setters do NOT mutate them — defaults from
// `TextAnimatorSpec` (`Add` for transforms, `Replace` for colours) are
// the engine's "AE-style" defaults and are preserved verbatim. Callers
// that need different blend modes can extend the façade in a follow-up.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/authoring/selector.hpp>

#include <string>
#include <utility>

namespace chronon3d::authoring {

// Forward-declare the test-only release accessor.  The struct itself is
// defined in `tests/authoring/test_animator_dsl.cpp` so this declaration
// is the only public surface needed by the builder headers.
namespace testing { struct AnimatorTestAccess; }

class Animator {
public:
    explicit Animator(std::string id) : spec_{} {
        spec_.id = std::move(id);
        spec_.selectors.reserve(2);
        spec_.properties.reserve(8);
    }

    Animator(const Animator&)            = delete;
    Animator& operator=(const Animator&) = delete;
    Animator(Animator&&)                 = default;
    Animator& operator=(Animator&&)      = default;

    // ── Selectors ───────────────────────────────────────────────────────
    Animator& select(Selector selector) & {
        spec_.selectors.emplace_back(std::move(selector).release());
        return *this;
    }

    // ── Transform properties (Spec default blend mode: Add) ─────────────
    Animator& position(Vec3 value) & {
        spec_.properties.emplace_back(PositionProperty{value});
        return *this;
    }
    Animator& scale(Vec3 value) & {
        spec_.properties.emplace_back(ScaleProperty{value});
        return *this;
    }
    /// Uniform scale convenience — produces `Vec3{u, u, 1.0f}` because
    /// most authoring flows animate a uniform word-mark/body, not
    /// anisotropic XYZ scales.
    Animator& scale(f32 uniform) & {
        spec_.properties.emplace_back(ScaleProperty{Vec3{uniform, uniform, 1.0f}});
        return *this;
    }
    Animator& rotation(Vec3 degrees) & {
        spec_.properties.emplace_back(RotationProperty{degrees});
        return *this;
    }
    Animator& skew(f32 angle_degrees, f32 axis_degrees = 0.0f) & {
        spec_.properties.emplace_back(SkewProperty{angle_degrees, axis_degrees});
        return *this;
    }
    Animator& anchor(Vec3 value) & {
        spec_.properties.emplace_back(AnchorProperty{value});
        return *this;
    }
    Animator& tracking(f32 pixels) & {
        spec_.properties.emplace_back(TrackingProperty{pixels});
        return *this;
    }
    Animator& baseline_shift(f32 pixels) & {
        spec_.properties.emplace_back(BaselineShiftProperty{pixels});
        return *this;
    }
    Animator& character_offset(i32 offset) & {
        spec_.properties.emplace_back(CharacterOffsetProperty{offset});
        return *this;
    }

    // ── Visual properties (Spec default blend mode: Replace) ────────────
    Animator& opacity(f32 value) & {
        spec_.properties.emplace_back(OpacityProperty{value});
        return *this;
    }
    Animator& blur(f32 radius_px) & {
        spec_.properties.emplace_back(BlurProperty{radius_px});
        return *this;
    }
    Animator& fill_color(Color color) & {
        spec_.properties.emplace_back(FillColorProperty{color});
        return *this;
    }
    Animator& stroke_color(Color color) & {
        spec_.properties.emplace_back(StrokeColorProperty{color});
        return *this;
    }
    Animator& stroke_width(f32 width_px) & {
        spec_.properties.emplace_back(StrokeWidthProperty{width_px});
        return *this;
    }

private:
    friend class Text;                                                    // PR 3
    friend struct chronon3d::authoring::testing::AnimatorTestAccess;      // tests

    [[nodiscard]]
    TextAnimatorSpec release() && {
        return std::move(spec_);
    }

    TextAnimatorSpec spec_;
};

inline Animator animator(std::string id) {
    return Animator{std::move(id)};
}

} // namespace chronon3d::authoring
