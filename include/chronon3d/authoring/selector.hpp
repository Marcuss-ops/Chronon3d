// ═══════════════════════════════════════════════════════════════════════════
// Selector — fluent authoring façade over `GlyphSelectorSpec`.
//
// Builds an `AnimatedValue`-keyed selector incrementally so user code does
// not have to interact with `AnimatedValue<f32>`, EasingCurve construction,
// or `std::vector<GlyphSelectorSpec>` directly.
//
// Construction goes through the `selector(unit)` factory:
//
//   auto s = selector(TextSelectorUnit::Grapheme)
//                .shape_smooth()
//                .exclude_spaces()
//                .start(Frame{0},    0.0f)
//                .start(Frame{24}, 100.0f, Easing::OutCubic);
//
// The builder is move-only. Consumers pull the spec out via the rvalue
// `release()` helper (see `friend` declarations below).
//
// ── C++20 ref-qualifier note ────────────────────────────────────────────
//
// We use single `&` ref-qualifier per setter (instead of duplicating
// `&`/`&&` pairs) because the only consumer chain — `Text::animate()`
// — takes the builder by value and `std::move`s it into the layer.
// Half the boilerplate, identical user-facing syntax at fluent call sites.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/glyph_selector.hpp>

#include <utility>

namespace chronon3d::authoring {

// Forward-declare the test-only release accessor. The struct itself is
// defined in `tests/authoring/test_animator_dsl.cpp` (which is the only
// place that should befriend `release()`).  Production consumers in PR
// 3+ (Text::animate etc.) get their own friend declarations.
namespace testing { struct AnimatorTestAccess; }

class Selector {
public:
    explicit Selector(TextSelectorUnit unit) : spec_{} {
        spec_.unit = unit;
    }

    Selector(const Selector&)            = delete;
    Selector& operator=(const Selector&) = delete;
    Selector(Selector&&)                 = default;
    Selector& operator=(Selector&&)      = default;

    // ── Shape / order / combine ─────────────────────────────────────────
    Selector& shape(TextSelectorShape value) & {
        spec_.shape = value;
        return *this;
    }
    Selector&& shape(TextSelectorShape value) && {
        spec_.shape = value;
        return std::move(*this);
    }
    /// Alias for `.shape(TextSelectorShape::Smooth)` — matches After
    /// Effects' "Smooth" selector shape in the UI.
    Selector& smooth() & {
        spec_.shape = TextSelectorShape::Smooth;
        return *this;
    }
    Selector&& smooth() && {
        spec_.shape = TextSelectorShape::Smooth;
        return std::move(*this);
    }
    Selector& order(TextSelectorOrder value) & {
        spec_.order = value;
        return *this;
    }
    Selector&& order(TextSelectorOrder value) && {
        spec_.order = value;
        return std::move(*this);
    }
    Selector& combine_mode(SelectorCombineMode value) & {
        spec_.combine = value;
        return *this;
    }
    Selector&& combine_mode(SelectorCombineMode value) && {
        spec_.combine = value;
        return std::move(*this);
    }

    // ── Space exclusion ─────────────────────────────────────────────────
    Selector& exclude_spaces(bool value = true) & {
        spec_.exclude_spaces = value;
        return *this;
    }
    Selector&& exclude_spaces(bool value = true) && {
        spec_.exclude_spaces = value;
        return std::move(*this);
    }

    // ── Keyframes (multiple calls accumulate, like `AnimatedValue::key()`)
    // Int-frame overloads exist so user code can write `.start(24, 1.0f)`
    // (matches the design doc verbatim). Frame-frame overloads exist so
    // arithmetic expressions like `.start(Frame{base + 12}, 1.0f)` work.
    Selector& start(int frame, f32 value, Easing easing = Easing::Linear) & {
        spec_.start.key(Frame{frame}, value, EasingCurve{easing});
        return *this;
    }
    Selector&& start(int frame, f32 value, Easing easing = Easing::Linear) && {
        spec_.start.key(Frame{frame}, value, EasingCurve{easing});
        return std::move(*this);
    }
    Selector& start(Frame frame, f32 value, Easing easing = Easing::Linear) & {
        spec_.start.key(frame, value, EasingCurve{easing});
        return *this;
    }
    Selector&& start(Frame frame, f32 value, Easing easing = Easing::Linear) && {
        spec_.start.key(frame, value, EasingCurve{easing});
        return std::move(*this);
    }
    Selector& end(int frame, f32 value, Easing easing = Easing::Linear) & {
        spec_.end.key(Frame{frame}, value, EasingCurve{easing});
        return *this;
    }
    Selector&& end(int frame, f32 value, Easing easing = Easing::Linear) && {
        spec_.end.key(Frame{frame}, value, EasingCurve{easing});
        return std::move(*this);
    }
    Selector& end(Frame frame, f32 value, Easing easing = Easing::Linear) & {
        spec_.end.key(frame, value, EasingCurve{easing});
        return *this;
    }
    Selector&& end(Frame frame, f32 value, Easing easing = Easing::Linear) && {
        spec_.end.key(frame, value, EasingCurve{easing});
        return std::move(*this);
    }
    Selector& offset(int frame, f32 value, Easing easing = Easing::Linear) & {
        spec_.offset.key(Frame{frame}, value, EasingCurve{easing});
        return *this;
    }
    Selector&& offset(int frame, f32 value, Easing easing = Easing::Linear) && {
        spec_.offset.key(Frame{frame}, value, EasingCurve{easing});
        return std::move(*this);
    }
    Selector& offset(Frame frame, f32 value, Easing easing = Easing::Linear) & {
        spec_.offset.key(frame, value, EasingCurve{easing});
        return *this;
    }
    Selector&& offset(Frame frame, f32 value, Easing easing = Easing::Linear) && {
        spec_.offset.key(frame, value, EasingCurve{easing});
        return std::move(*this);
    }
    Selector& amount(int frame, f32 value, Easing easing = Easing::Linear) & {
        spec_.amount.key(Frame{frame}, value, EasingCurve{easing});
        return *this;
    }
    Selector&& amount(int frame, f32 value, Easing easing = Easing::Linear) && {
        spec_.amount.key(Frame{frame}, value, EasingCurve{easing});
        return std::move(*this);
    }
    Selector& amount(Frame frame, f32 value, Easing easing = Easing::Linear) & {
        spec_.amount.key(frame, value, EasingCurve{easing});
        return *this;
    }
    Selector&& amount(Frame frame, f32 value, Easing easing = Easing::Linear) && {
        spec_.amount.key(frame, value, EasingCurve{easing});
        return std::move(*this);
    }

    // ── Ease-in / ease-out break points (0..100 %) ──────────────────────
    Selector& ease_low(f32 percent) & {
        spec_.ease_low = percent;
        return *this;
    }
    Selector&& ease_low(f32 percent) && {
        spec_.ease_low = percent;
        return std::move(*this);
    }
    Selector& ease_high(f32 percent) & {
        spec_.ease_high = percent;
        return *this;
    }
    Selector&& ease_high(f32 percent) && {
        spec_.ease_high = percent;
        return std::move(*this);
    }

private:
    friend class Animator;                                // PR 1 release path
    friend struct chronon3d::authoring::testing::AnimatorTestAccess;  // tests only

    [[nodiscard]]
    GlyphSelectorSpec release() && {
        return std::move(spec_);
    }

    GlyphSelectorSpec spec_;
};

inline Selector selector(TextSelectorUnit unit) {
    return Selector{unit};
}

} // namespace chronon3d::authoring
