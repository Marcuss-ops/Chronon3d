#pragma once

#include <chronon3d/animation/core/animation_track.hpp>
#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/expression.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cmath>
#include <type_traits>

// Helper: detect glm vector types (vec2, vec3, vec4) for spatial roving.
template<typename T>
inline constexpr bool is_glm_vec_type_v =
    std::is_same_v<T, glm::vec2> ||
    std::is_same_v<T, glm::vec3> ||
    std::is_same_v<T, glm::vec4>;

/// Concept for types that can be animated by AnimatedValue<T>.
/// Requires default construction and copyability — the minimal contract
/// that every animated value (f32, Vec3, Color, FillStyle, etc.) satisfies.
template<typename T>
concept AnimatableValue =
    std::is_default_constructible_v<T> &&
    std::is_copy_constructible_v<T>;

namespace chronon3d {

// Generic component-wise lerp used by AnimatedValue<T>.
template <typename T>
inline T lerp(const T& a, const T& b, f32 t) {
    return a + (b - a) * t;
}

enum class LoopMode {
    Hold,
    Loop,
    PingPong
};

struct AnimationEvalContext {
    f32 fps{30.0f};
    f32 time{0.0f};
    bool has_explicit_time{false};
    int index{0};

    // ── Extended context for AE-style expressions (AE-1 + AE-10) ──
    // When set, expressions can use layer('name').property, thisComp.*, etc.
    const math::ExpressionContext* expression_context{nullptr};
};

// ── CP-A forward declarations ──────────────────────────────────────────────
// AnimatedValue<T>::evaluate(SampleTime, const AnimationEvalContext&) calls
// ::chronon3d::evaluate_fill_expression and ::chronon3d::evaluate_stroke_expression
// as NON-DEPENDENT names.  C++ two-phase name lookup (phase 1 = template
// definition, phase 2 = instantiation) requires those symbols to be visible
// at template definition time INSIDE the lexical scope — i.e. declared in
// ::chronon3d:: BEFORE the class AnimatedValue<T> body parses.  Without this
// fwdecl, the reference inside `if constexpr (std::is_same_v<T, FillStyle>)`
// branches resolves to "not a member of chronon3d" at parse time, even
// though the user-visible behaviour happens to be correct.
//
// Full declarations (with detailed docs) live in
// <chronon3d/animation/core/detail/animated_value_expressions.hpp> (included
// at the bottom of this file); these fwdecls only exist to bridge phase 1
// lookup before that bottom include is processed.
[[nodiscard]] graphics::FillStyle evaluate_fill_expression(
    const std::string& expr,
    const graphics::FillStyle& base,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame);

[[nodiscard]] graphics::StrokeStyle evaluate_stroke_expression(
    const std::string& expr,
    const graphics::StrokeStyle& base,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame);

// Note: the four expression-related free functions
// (::chronon3d::detail::split_expr_args +
//  ::chronon3d::detail::evaluate_solid_color_expression +
//  ::chronon3d::evaluate_fill_expression +
//  ::chronon3d::evaluate_stroke_expression)
// + Graphics FillStyle / StrokeStyle evaluation helpers
// are declared in <chronon3d/animation/core/detail/animated_value_expressions.hpp>,
// included at the bottom of this header.

template <AnimatableValue T>
class AnimatedValue {
public:
    AnimatedValue() = default;
    // Constructor is intentionally NOT `explicit` so callers can use
    // single-brace aggregate initialisation with an inner scalar:
    // `OpacityProperty{1.0f}`, `ScaleProperty{Vec3{1,1,1}}`.  Without
    // this, aggregate-init would request a non-explicit T → AnimatedValue<T>
    // conversion that C++ copy-init rules forbid.  Behaviourally
    // identical to the keyframe path (every AnimatedValue<T> field is
    // already key-derivable from a scalar), so dropping `explicit` is a
    // backward-compatible widening with no production-side behaviour
    // change.
    AnimatedValue(T default_value) : m_default_value(default_value) {}

    /// Construct from an initializer list of Keyframes.
    /// Enables `keyframes<T>({{0, v0}, {60, v1}})` factory syntax.
    AnimatedValue(std::initializer_list<Keyframe<T>> kfs)
        : m_keyframes(kfs) {
        std::sort(m_keyframes.begin(), m_keyframes.end());
        for (const auto& kf : m_keyframes) {
            if (kf.roving) { m_roving_dirty = true; break; }
        }
    }

    AnimatedValue& loop_mode(LoopMode mode) {
        m_loop_mode = mode;
        return *this;
    }

    [[nodiscard]] LoopMode loop_mode() const {
        return m_loop_mode;
    }

    // Add a keyframe; keys are kept sorted by frame.
    void add_keyframe(Frame frame, const T& value, EasingCurve easing = EasingCurve{Easing::Linear}, bool roving = false) {
        m_keyframes.emplace_back(frame, value, easing, roving);
        std::sort(m_keyframes.begin(), m_keyframes.end());
        if (roving) m_roving_dirty = true;
    }

    /// Add a keyframe with InterpMode + temporal tangent handles.
    void add_keyframe(Frame frame, const T& value, InterpMode interp,
                       f32 in_dx, f32 in_dy, f32 out_dx, f32 out_dy) {
        m_keyframes.emplace_back(frame, value, EasingCurve{Easing::Linear}, false);
        m_keyframes.back().interp = interp;
        m_keyframes.back().temporal_in_dx = in_dx;
        m_keyframes.back().temporal_in_dy = in_dy;
        m_keyframes.back().temporal_out_dx = out_dx;
        m_keyframes.back().temporal_out_dy = out_dy;
        std::sort(m_keyframes.begin(), m_keyframes.end());
        if (interp == InterpMode::AutoBezier) m_auto_bezier_dirty = true;
    }

    // Canonical fluent form for adding a keyframe — returns *this to
    // enable chaining (e.g. `lb.position_anim().key(0, ...).key(30, ...)`).
    // The void-returning `add_keyframe()` overloads above remain available
    // for non-chaining call sites; both share the same insertion semantics.
    AnimatedValue& key(Frame frame, const T& value, EasingCurve easing = EasingCurve{Easing::Linear}) {
        add_keyframe(frame, value, easing);
        return *this;
    }

    /// Canonical fluent form for adding a keyframe with bezier handles —
    /// returns *this to enable chaining. Companion to the 3-arg `key()`
    /// overload immediately above.
    AnimatedValue& key(Frame frame, const T& value, EasingCurve easing,
                        T out_handle, T in_handle) {
        m_keyframes.emplace_back(frame, value, easing, false);
        m_keyframes.back().out_handle = out_handle;
        m_keyframes.back().in_handle = in_handle;
        std::sort(m_keyframes.begin(), m_keyframes.end());
        return *this;
    }

    // Mark the last-added keyframe as roving (constant velocity).
    AnimatedValue& roving() {
        if (!m_keyframes.empty()) {
            m_keyframes.back().roving = true;
            m_roving_dirty = true;
        }
        return *this;
    }

    // Recompute roving keyframe timing for constant velocity.
    // Implementation lives in detail/animated_value_roving.inl.
    void compute_roving() const;

    /// Auto-compute temporal bezier tangents for AutoBezier keyframes.
    /// Like AnimationCurve::compute_auto_beziers() — only meaningful for
    /// arithmetic (scalar) types.
    void compute_auto_beziers() const;

    // Set a constant value (clears all keyframes and expressions).
    AnimatedValue& set(const T& value) {
        clear();
        m_default_value = value;
        m_expression.clear();
        return *this;
    }

    AnimatedValue& operator=(const T& value) {
        return set(value);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Expressions V1 — honest contract (Path A)
    //
    //  Supported in V1:
    //    • Scalars (`AnimatedValue<f32>`) with full `math::ExpressionParser`
    //      coverage — arithmetic, trig, `linear`/`ease`/`easeIn`/`easeOut`,
    //      `wiggle`, `random`, `seedRandom`, `loopIn`/`loopOut`, cross-layer
    //      `layer("name").property`, `thisComp.*`, `thisLayer.*`,
    //      `thisProperty.*`.
    //    • `AnimatedValue<graphics::FillStyle>` and
    //      `AnimatedValue<graphics::StrokeStyle>` — the dedicated `solid(r,g,b,a)`
    //      expression. Each argument is itself a numeric expression with the
    //      same `frame`/`time`/`fps`/`index` variables. Parser errors fall back
    //      to the base style (silently, per existing test contract).
    //
    //  NOT supported in V1 (returns the base value unchanged):
    //    • `AnimatedValue<Vec2>` / `AnimatedValue<Vec3>` /
    //      `AnimatedValue<Vec4>` — no `position`/`scale`/`transform`
    //      expressions yet.
    //    • `AnimatedValue<Color>` (raw, not via style) — use
    //      `AnimatedValue<FillStyle>` + `solid(...)`.
    //    • `AnimatedValue<glm::mat*>` — no matrix expressions.
    //
    //  Path B (AE-oriented v1 with `std::variant` + lexer/AST/bytecode/VM
    //  + cycle detection) is tracked as a roadmap item. Until path B lands,
    //  the existing recursive-descent scalar parser must remain the only
    //  evaluation entry point: do not add functions that pretend to support
    //  Vec/Color/Transform types when the underlying return is `double`.
    // ═════════════════════════════════════════════════════════════════════
    AnimatedValue& expression(std::string expr) {
        m_expression = std::move(expr);
        return *this;
    }

    [[nodiscard]] const std::string& expression() const { return m_expression; }
    [[nodiscard]] bool has_expression() const { return !m_expression.empty(); }

    // ── Backward-compatible methods (KeyframeTrack API surface) ─────────────
    [[nodiscard]] T sample_at(f32 current) const {
        return evaluate_base_double(static_cast<double>(current));
    }
    [[nodiscard]] T sample(Frame current) const { return evaluate(current); }
    [[nodiscard]] T value(Frame current) const { return evaluate(current); }
    [[nodiscard]] T value_at_time(f32 current) const {
        return evaluate_base_double(static_cast<double>(current));
    }
    [[nodiscard]] T operator()(Frame current) const { return evaluate(current); }
    [[nodiscard]] bool empty() const { return m_keyframes.empty(); }
    [[nodiscard]] std::size_t size() const { return m_keyframes.size(); }

    // ── Frame evaluation (backward compatible + forward) ────────────────────
    [[nodiscard]] T value_at(Frame frame) const { return evaluate(frame); }

    /// Integer-literal disambiguation: delegates to evaluate(Frame).
    /// Resolves ambiguity between evaluate(double) and evaluate(Frame)
    /// when passing integer literals (e.g. evaluate(30)).
    [[nodiscard]] T evaluate(int frame) const {
        return evaluate(Frame{frame});
    }

    /// Double-precision evaluation (for TemporalCurve1D compatibility).
    [[nodiscard]] T evaluate(double frame) const {
        return evaluate_base_double(frame);
    }

    // Sub-frame evaluation (SampleTime) — the future
    [[nodiscard]] T evaluate(SampleTime time) const {
        return evaluate(time, AnimationEvalContext{});
    }

    [[nodiscard]] T evaluate(SampleTime time, const AnimationEvalContext& ctx) const {
        const T base = evaluate_base_double(time.frame);

        if (!has_expression()) return base;

        if constexpr (std::is_same_v<T, f32>) {
            const double fps = time.fps();
            const double t = time.seconds();
            const double frame = time.frame;

            if (ctx.expression_context) {
                math::ExpressionContext ectx = *ctx.expression_context;
                ectx.value  = static_cast<double>(base);
                ectx.value0 = static_cast<double>(base);
                ectx.frame  = frame;
                ectx.time   = t;
                ectx.fps    = fps;
                ectx.index  = static_cast<double>(ctx.index);
                return static_cast<f32>(
                    math::evaluate_expression(m_expression, ectx, {}, static_cast<double>(base))
                );
            }

            const std::unordered_map<std::string, double> vars{
                {"frame", frame},
                {"time", t},
                {"value", static_cast<double>(base)},
                {"index", static_cast<double>(ctx.index)},
            };
            return static_cast<f32>(
                math::evaluate_expression(m_expression, vars, static_cast<double>(base))
            );
        } else if constexpr (std::is_same_v<T, graphics::FillStyle>) {
            const double fps = time.fps();
            const double t = time.seconds();
            const double frame = time.frame;
            // Qualified (::chronon3d::) so the lookup is explicit at parse
            // time even when readers inspect this header without the
            // bottom-#include of detail/animated_value_expressions.hpp
            // having been processed.
            return ::chronon3d::evaluate_fill_expression(
                m_expression, base, ctx,
                static_cast<f32>(fps), t, frame);
        } else if constexpr (std::is_same_v<T, graphics::StrokeStyle>) {
            const double fps = time.fps();
            const double t = time.seconds();
            const double frame = time.frame;
            return ::chronon3d::evaluate_stroke_expression(
                m_expression, base, ctx,
                static_cast<f32>(fps), t, frame);
        }
        return base;
    }

    // Frame evaluation (backward compatible)
    [[nodiscard]] T evaluate(Frame frame) const {
        return evaluate(SampleTime::from_frame_int(frame, FrameRate{30, 1}));
    }

    [[nodiscard]] T evaluate(Frame frame, const AnimationEvalContext& ctx) const {
        const f32 fps_val = (ctx.fps > 0.0f) ? ctx.fps : 30.0f;
        const auto fps_ms = static_cast<i32>(std::llround(static_cast<double>(fps_val) * 1000.0));
        return evaluate(SampleTime::from_frame_int(frame, FrameRate{fps_ms, 1000}), ctx);
    }

    [[nodiscard]] bool is_animated() const { return !m_keyframes.empty(); }

    /// Returns true if the value depends on time (keyframes or expression).
    /// Use this for cache invalidation, scene hashing, and dirty-rect decisions.
    /// Unlike is_animated(), this also covers expression-only properties
    /// that have no keyframes but still change over time.
    [[nodiscard]] bool is_time_dependent() const {
        return is_animated() || has_expression();
    }

    /**
     * @brief Determines if evaluating this property at a specific frame is "expensive"
     * enough to justify caching.
     */
    [[nodiscard]] bool should_cache(Frame frame) const { return should_cache_double(static_cast<double>(frame)); }

    [[nodiscard]] bool should_cache(const SampleTime& time) const { return should_cache_double(time.frame); }

    /// Returns the time (frame) of the first keyframe, or Frame{0} if empty.
    [[nodiscard]] Frame first_keyframe_time() const {
        if (m_keyframes.empty()) return Frame{0};
        return m_keyframes.front().frame;
    }

    /// Returns the time (frame) of the last keyframe, or Frame{0} if empty.
    /// Useful for determining when an animation has reached its terminal state.
    [[nodiscard]] Frame last_keyframe_time() const {
        if (m_keyframes.empty()) return Frame{0};
        return m_keyframes.back().frame;
    }

    /// Read-only access to keyframes (for TemporalCurve1D compatibility).
    [[nodiscard]] const std::vector<Keyframe<T>>& keyframes() const {
        return m_keyframes;
    }

    void clear() { m_keyframes.clear(); m_roving_dirty = true; m_auto_bezier_dirty = false; }

    // ── AnimationTrack<T> declarative builder────────────────────────
    // Bake an AnimationTrack<T>'s keyframes into this AnimatedValue, replacing
    // any previous keyframe schedule. Mirrors the Framer Motion / React-Spring
    // / Remotion declarative-builder pattern: build the curve once at
    // composition construction time, then apply the same track to N animated
    // properties (e.g. title.scale_anim() and title_shadow.scale_anim()
    // sharing one AnimationTrack<float>).
    //
    // Each keyframe's .easing curve is preserved; roving=false (the
    // AnimationTrack model doesn't carry roving). Sorted by Frame after bake
    // so evaluate_base_double() sees its sorted-invariant.
    AnimatedValue& apply_track(const AnimationTrack<T>& track) {
        clear();
        for (const auto& kf : track.keys()) {
            m_keyframes.emplace_back(kf.frame, kf.value, kf.easing, /*roving=*/false);
        }
        std::sort(m_keyframes.begin(), m_keyframes.end());
        return *this;
    }

    // ── Vec3 single-axis drivers from a scalar AnimationTrack────────────
    // SFINAE: only available when AnimatedValue stores a 3-vector type
    // (Vec3 from glm_types.hpp or glm::vec3). The driven axis varies with the
    // scalar track; the other two axes hold the supplied defaults (1.0f by
    // default — the typical choice for "scale" / "perspective" tracks).
    // Previous keyframe schedule is cleared.
    template <typename U = T,
              typename = std::enable_if_t<
                  std::is_same_v<U, Vec3> || std::is_same_v<U, glm::vec3>>>
    AnimatedValue& animate_x(const AnimationTrack<float>& track,
                              f32 y_default = 1.0f,
                              f32 z_default = 1.0f) {
        clear();
        for (const auto& kf : track.keys()) {
            m_keyframes.emplace_back(
                kf.frame,
                T{static_cast<f32>(kf.value), y_default, z_default},
                kf.easing, /*roving=*/false);
        }
        std::sort(m_keyframes.begin(), m_keyframes.end());
        return *this;
    }

    template <typename U = T,
              typename = std::enable_if_t<
                  std::is_same_v<U, Vec3> || std::is_same_v<U, glm::vec3>>>
    AnimatedValue& animate_y(const AnimationTrack<float>& track,
                              f32 x_default = 1.0f,
                              f32 z_default = 1.0f) {
        clear();
        for (const auto& kf : track.keys()) {
            m_keyframes.emplace_back(
                kf.frame,
                T{x_default, static_cast<f32>(kf.value), z_default},
                kf.easing, /*roving=*/false);
        }
        std::sort(m_keyframes.begin(), m_keyframes.end());
        return *this;
    }

    template <typename U = T,
              typename = std::enable_if_t<
                  std::is_same_v<U, Vec3> || std::is_same_v<U, glm::vec3>>>
    AnimatedValue& animate_z(const AnimationTrack<float>& track,
                              f32 x_default = 1.0f,
                              f32 y_default = 1.0f) {
        clear();
        for (const auto& kf : track.keys()) {
            m_keyframes.emplace_back(
                kf.frame,
                T{x_default, y_default, static_cast<f32>(kf.value)},
                kf.easing, /*roving=*/false);
        }
        std::sort(m_keyframes.begin(), m_keyframes.end());
        return *this;
    }

    /// Shift every keyframe forward (or backward) by offset frames.
    /// Keyframes are clamped at Frame{0} to prevent negative frame indices.
    void shift(Frame offset) {
        for (auto& kf : m_keyframes) {
            kf.frame = std::max(Frame{0}, kf.frame + offset);
        }
    }

private:
    // Check if any handle is non-zero (spatial bezier mode). Body in evaluation.inl.
    [[nodiscard]] static bool has_spatial_handles(const Keyframe<T>& kf);

    // Core interpolation engine — works with double precision for sub-frame accuracy.
    [[nodiscard]] T evaluate_base_double(double frame) const;

    // ── Temporal bezier evaluation (scalar only) ─────────────────────────
    // Uses Newton-Raphson to solve x(u) = t, then returns y(u).
    // Body in detail/animated_value_bezier.inl.
    [[nodiscard]] static T eval_temporal_bezier(
        const Keyframe<T>& prev, const Keyframe<T>& next, f32 t);

    bool should_cache_double(double frame) const;

    // Legacy: delegates to double precision engine
    [[nodiscard]] T evaluate_base(Frame frame) const {
        return evaluate_base_double(static_cast<double>(frame));
    }

    T m_default_value{};
    mutable std::vector<Keyframe<T>> m_keyframes;
    LoopMode m_loop_mode{LoopMode::Hold};
    std::string m_expression;
    mutable bool m_roving_dirty{false};
    mutable bool m_auto_bezier_dirty{false};
};

// ── KeyframeTrack<T> — backward-compatible alias for AnimatedValue<T> ────
// All KeyframeTrack usage (keyframes<T>(), .sample(), .sample_at(), .value(), etc.)
// now operates on AnimatedValue<T> with the full engine (expressions, loops, roving).
template<typename T> using KeyframeTrack = AnimatedValue<T>;

// keyframes<T>() — factory for creating AnimatedValue<T> from a keyframe list.
// Usage:
//   auto x = keyframes<f32>({ {0, 0.0f}, {60, 100.0f, Easing::OutCubic} }).sample(frame);
//   auto x = keyframes<f32>({}).key(0, -300.0f, Easing::OutCubic).key(40, 0.0f);
//
template <typename T>
inline AnimatedValue<T> keyframes(std::initializer_list<Keyframe<T>> kfs) {
    return AnimatedValue<T>(kfs);
}

// Legacy support: keyframes(frame, {KF, KF, ...})
inline f32 keyframes(Frame current, std::initializer_list<KF> kfs) {
    return keyframes<f32>(kfs).sample(current);
}

// ==============================================================================
// CP-A (snapshot: docs/baselines/main-9ef0fe33-dod-fail-matrix.md, 2026-06-29):
// Phase-3 mechanical split — include detail implementations INSIDE the
// `namespace chronon3d { ... }` block so the .inl template method
// definitions (e.g. `AnimatedValue<T>::compute_roving()`) observe
// `AnimatedValue<T>` in scope.  Previously these includes sat AFTER
// `} // namespace chronon3d`, leaving the qualifier unresolved in the
// global namespace and producing `expected initializer before '<' token`
// at the `AnimatedValue<T>::method(...)` lines.  Order is preserved
// (expressions → bezier → roving → evaluation) per the prior invariants:
//   • roving depends on bezier (compute_roving → compute_auto_beziers)
//   • evaluation depends on bezier (eval_temporal_bezier) AND roving
//   • expressions forwards public FillStyle/StrokeStyle declarations
//
// IMPORTANT: expressions.hpp is included OUTSIDE `namespace chronon3d`
// because it opens its own `namespace chronon3d { }` block.  If included
// INSIDE the outer namespace block, this creates a nested
// `chronon3d::chronon3d` namespace (the `expressions.hpp` namespace is
// NOT merged with the outer one — nested-namespace rules apply).  The
// .inl files do NOT open a namespace and must remain inside.
// ==============================================================================
#include <chronon3d/animation/core/detail/animated_value_bezier.inl>
#include <chronon3d/animation/core/detail/animated_value_roving.inl>
#include <chronon3d/animation/core/detail/animated_value_evaluation.inl>

} // namespace chronon3d

#include <chronon3d/animation/core/detail/animated_value_expressions.hpp>
