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

// ── FillStyle expression evaluation ─────────────────────────────────────
// Parses expressions of the form: solid(r, g, b, a)
// Each argument is itself a numeric expression supporting variables:
//   frame, time, fps, index
// Returns the base value on parse failure or non-solid expression.

namespace detail {

/// Split function arguments by commas, respecting nested parentheses.
[[nodiscard]] std::vector<std::string> split_expr_args(
    const std::string& inner);

/// Parse a solid(r, g, b, a) expression into a Color.
/// Each argument is evaluated as a numeric expression with variables:
///   frame, time, fps, index
/// Returns std::nullopt if the expression is not a valid solid(...) call.
[[nodiscard]] std::optional<Color> evaluate_solid_color_expression(
    std::string_view expr,
    const AnimationEvalContext& ctx,
    f32 fps,
    double t,
    double frame);

} // namespace detail

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

template <AnimatableValue T>
class AnimatedValue {
public:
    AnimatedValue() = default;
    explicit AnimatedValue(T default_value) : m_default_value(default_value) {}

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

    // Fluent alias for add_keyframe — enables chaining.
    AnimatedValue& key(Frame frame, const T& value, EasingCurve easing = EasingCurve{Easing::Linear}) {
        add_keyframe(frame, value, easing);
        return *this;
    }

    /// Add a keyframe with spatial bezier handles for smooth curved paths.
    /// The handles are offsets from `value` that control the cubic bezier path:
    ///   out_handle → outgoing tangent direction from this keyframe
    ///   in_handle  → incoming tangent direction toward the next keyframe
    /// When both handles are zero (default), straight-line interpolation is used.
    /// When non-zero, the segment between consecutive keyframes uses a
    /// CubicBezier3D curve.
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
    // For each group of consecutive roving keyframes between two non-roving
    // anchors, redistributes their frames so velocity is constant.
    //
    // This method is callable on const objects because it's a lazy cache
    // operation triggered automatically during evaluate().  The mutable
    // qualifiers on m_keyframes and m_roving_dirty allow this.
    void compute_roving() const {
        if (!m_roving_dirty) return;
        if (m_keyframes.size() < 3) { m_roving_dirty = false; return; }

        // Auto-compute bezier tangents before roving (like AnimationCurve).
        compute_auto_beziers();

        std::sort(m_keyframes.begin(), m_keyframes.end());

        size_t i = 0;
        while (i < m_keyframes.size()) {
            if (!m_keyframes[i].roving) { ++i; continue; }

            // Find left anchor (closest non-roving to the left)
            size_t left = (i > 0) ? (i - 1) : 0;
            while (left > 0 && m_keyframes[left].roving) --left;
            if (m_keyframes[left].roving) {
                m_keyframes[i].roving = false;
                ++i;
                continue;
            }

            // Find right anchor (next non-roving to the right)
            size_t right = i;
            while (right < m_keyframes.size() && m_keyframes[right].roving) ++right;
            if (right >= m_keyframes.size()) {
                for (size_t j = i; j < m_keyframes.size(); ++j)
                    m_keyframes[j].roving = false;
                break;
            }

            // Constant velocity between left and right anchors
            const f32 t_left = static_cast<f32>(m_keyframes[left].frame);
            const f32 t_right = static_cast<f32>(m_keyframes[right].frame);
            const f32 dt = t_right - t_left;

            if constexpr (std::is_arithmetic_v<T>) {
                const f32 v_left = static_cast<f32>(m_keyframes[left].value);
                const f32 v_right = static_cast<f32>(m_keyframes[right].value);
                const f32 dv = v_right - v_left;

                if (std::abs(dv) < 1e-7f || dt <= 0.0f) {
                    const size_t count = right - left - 1;
                    for (size_t j = 0; j < count; ++j) {
                        f32 frac = static_cast<f32>(j + 1) / static_cast<f32>(count + 1);
                        m_keyframes[left + 1 + j].frame =
                            Frame{static_cast<i32>(std::round(t_left + frac * dt))};
                    }
                } else {
                    const f32 velocity = dv / dt;
                    for (size_t j = left + 1; j < right; ++j) {
                        const f32 val_dist = static_cast<f32>(m_keyframes[j].value) - v_left;
                        const f32 target = t_left + val_dist / velocity;
                        m_keyframes[j].frame = Frame{static_cast<i32>(
                            std::round(std::clamp(target, t_left + 1.0f, t_right - 1.0f))
                        )};
                    }
                }
            } else if constexpr (is_glm_vec_type_v<T>) {
                // Spatial types (Vec3, etc.): roving based on spatial distance
                const f32 total_dist = glm::length(m_keyframes[right].value - m_keyframes[left].value);
                if (total_dist < 1e-7f || dt <= 0.0f) {
                    const size_t count = right - left - 1;
                    for (size_t j = 0; j < count; ++j) {
                        f32 frac = static_cast<f32>(j + 1) / static_cast<f32>(count + 1);
                        m_keyframes[left + 1 + j].frame =
                            Frame{static_cast<i32>(std::round(t_left + frac * dt))};
                    }
                } else {
                    for (size_t j = left + 1; j < right; ++j) {
                        const f32 dist_from_left = glm::length(m_keyframes[j].value - m_keyframes[left].value);
                        const f32 frac = dist_from_left / total_dist;
                        const f32 target = t_left + frac * dt;
                        m_keyframes[j].frame = Frame{static_cast<i32>(
                            std::round(std::clamp(target, t_left + 1.0f, t_right - 1.0f))
                        )};
                    }
                }
            } else {
                // Non-arithmetic, non-spatial T: distribute frames evenly
                const size_t count = right - left - 1;
                for (size_t j = 0; j < count; ++j) {
                    f32 frac = static_cast<f32>(j + 1) / static_cast<f32>(count + 1);
                    m_keyframes[left + 1 + j].frame =
                        Frame{static_cast<i32>(std::round(t_left + frac * dt))};
                }
            }

            i = right + 1;
        }

        m_roving_dirty = false;

        // Re-sort after frame modifications to maintain sorted invariant
        std::sort(m_keyframes.begin(), m_keyframes.end());
    }

    /// Auto-compute temporal bezier tangents for AutoBezier keyframes.
    /// Like AnimationCurve::compute_auto_beziers() — only meaningful for
    /// arithmetic (scalar) types.
    void compute_auto_beziers() const {
        if (!m_auto_bezier_dirty) return;
        if (m_keyframes.size() < 3) { m_auto_bezier_dirty = false; return; }

        for (size_t i = 0; i < m_keyframes.size(); ++i) {
            auto& kf = m_keyframes[i];
            if (kf.interp != InterpMode::AutoBezier) continue;

            const bool has_prev = (i > 0);
            const bool has_next = (i + 1 < m_keyframes.size());

            constexpr f32 kTension = 1.0f / 3.0f;

            if (has_prev && has_next) {
                const f32 dt_prev = static_cast<f32>(kf.frame - m_keyframes[i - 1].frame);
                const f32 dt_next = static_cast<f32>(m_keyframes[i + 1].frame - kf.frame);

                if constexpr (std::is_arithmetic_v<T>) {
                    const f32 dv_prev = static_cast<f32>(kf.value - m_keyframes[i - 1].value);
                    const f32 dv_next = static_cast<f32>(m_keyframes[i + 1].value - kf.value);
                    const f32 slope = (dv_prev + dv_next) / (dt_prev + dt_next);

                    kf.temporal_in_dx  = -dt_prev * kTension;
                    kf.temporal_in_dy  = -slope * dt_prev * kTension;
                    kf.temporal_out_dx = dt_next * kTension;
                    kf.temporal_out_dy = slope * dt_next * kTension;
                } else {
                    kf.temporal_in_dx = 0.0f;
                    kf.temporal_in_dy = 0.0f;
                    kf.temporal_out_dx = 0.0f;
                    kf.temporal_out_dy = 0.0f;
                }
            } else if (has_prev) {
                if constexpr (std::is_arithmetic_v<T>) {
                    const f32 dt = static_cast<f32>(kf.frame - m_keyframes[i - 1].frame);
                    const f32 dv = static_cast<f32>(kf.value - m_keyframes[i - 1].value);
                    const f32 slope = (dt > 0.0f) ? (dv / dt) : 0.0f;
                    kf.temporal_in_dx  = -dt * kTension;
                    kf.temporal_in_dy  = -slope * dt * kTension;
                }
                kf.temporal_out_dx = 0.0f;
                kf.temporal_out_dy = 0.0f;
            } else if (has_next) {
                if constexpr (std::is_arithmetic_v<T>) {
                    const f32 dt = static_cast<f32>(m_keyframes[i + 1].frame - kf.frame);
                    const f32 dv = static_cast<f32>(m_keyframes[i + 1].value - kf.value);
                    const f32 slope = (dt > 0.0f) ? (dv / dt) : 0.0f;
                    kf.temporal_out_dx = dt * kTension;
                    kf.temporal_out_dy = slope * dt * kTension;
                }
                kf.temporal_in_dx = 0.0f;
                kf.temporal_in_dy = 0.0f;
            }
        }
        m_auto_bezier_dirty = false;
    }

    // Set a constant value (clears all keyframes).
    AnimatedValue& set(const T& value) {
        clear();
        m_default_value = value;
        m_expression.clear();
        return *this;
    }

    AnimatedValue& operator=(const T& value) {
        return set(value);
    }

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
            return evaluate_fill_expression(
                m_expression, base, ctx,
                static_cast<f32>(fps), t, frame);
        } else if constexpr (std::is_same_v<T, graphics::StrokeStyle>) {
            const double fps = time.fps();
            const double t = time.seconds();
            const double frame = time.frame;
            return evaluate_stroke_expression(
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
    // Check if any handle is non-zero (spatial bezier mode).
    [[nodiscard]] static bool has_spatial_handles(const Keyframe<T>& kf) {
        if constexpr (std::is_same_v<T, Vec3> || std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec4>) {
            return glm::length2(kf.out_handle) > 1e-12f || glm::length2(kf.in_handle) > 1e-12f;
        }
        return false;
    }

    // Core interpolation engine — works with double precision for sub-frame accuracy.
    [[nodiscard]] T evaluate_base_double(double frame) const {
        // Auto-compute beziers and roving before evaluation if dirty.
        if (m_auto_bezier_dirty) {
            compute_auto_beziers();
        }
        if (m_roving_dirty) {
            compute_roving();
        }
        if (m_keyframes.empty()) {
            return m_default_value;
        }

        const double start_f = static_cast<double>(m_keyframes.front().frame);
        const double end_f   = static_cast<double>(m_keyframes.back().frame);
        const double range   = end_f - start_f;

        double eval_frame = frame;

        if (m_loop_mode == LoopMode::Loop && range > 0) {
            if (frame < start_f) {
                eval_frame = end_f - std::fmod(start_f - frame, range);
            } else {
                eval_frame = start_f + std::fmod(frame - start_f, range);
            }
        } else if (m_loop_mode == LoopMode::PingPong && range > 0) {
            double t = (frame < start_f) ? (start_f - frame) : (frame - start_f);
            double cycle = std::floor(t / range);
            double offset = std::fmod(t, range);
            if (static_cast<int64_t>(cycle) % 2 == 0) {
                eval_frame = start_f + offset;
            } else {
                eval_frame = end_f - offset;
            }
        } else {
            if (frame <= start_f) return m_keyframes.front().value;
            if (frame >= end_f)   return m_keyframes.back().value;
        }

        // Linear scan for keyframe segment (uses > to correctly handle
        // duplicate frames — matches upper_bound behavior).
        size_t idx = 0;
        for (size_t i = 0; i + 1 < m_keyframes.size(); ++i) {
            if (static_cast<double>(m_keyframes[i + 1].frame) > eval_frame) {
                idx = i;
                break;
            }
            idx = i + 1;
        }
        if (idx + 1 >= m_keyframes.size()) {
            return m_keyframes.back().value;
        }

        const auto& prev = m_keyframes[idx];
        const auto& next = m_keyframes[idx + 1];

        const double prev_f = static_cast<double>(prev.frame);
        const double next_f = static_cast<double>(next.frame);
        if (next_f <= prev_f) return prev.value;

        const f32 t = static_cast<f32>((eval_frame - prev_f) / (next_f - prev_f));

        // Hold interpolation: value jumps at keyframe.
        if (prev.interp == InterpMode::Hold) return prev.value;

        // Temporal bezier interpolation (arithmetic/scalar types).
        if constexpr (std::is_arithmetic_v<T>) {
            if (prev.interp == InterpMode::Bezier || prev.interp == InterpMode::AutoBezier) {
                return eval_temporal_bezier(prev, next, t);
            }
        }

        // Spatial bezier path: when handles are present, use CubicBezier3D
        // for smooth curved interpolation through 3+ points.
        if constexpr (std::is_same_v<T, Vec3> || std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec4>) {
            if (has_spatial_handles(prev) || has_spatial_handles(next)) {
                const f32 eased_t = prev.easing.apply(std::clamp(t, 0.0f, 1.0f));
                // Build CubicBezier3D from the four control points
                const Vec3 p0 = prev.value;
                const Vec3 p1 = prev.value + prev.out_handle;
                const Vec3 p3 = next.value;
                const Vec3 p2 = next.value + next.in_handle;
                // De Casteljau evaluation
                const f32 u = 1.0f - eased_t;
                return u * u * u * p0
                     + 3.0f * u * u * eased_t * p1
                     + 3.0f * u * eased_t * eased_t * p2
                     + eased_t * eased_t * eased_t * p3;
            }
        }

        return interpolate_values(prev.value, next.value, std::clamp(t, 0.0f, 1.0f), prev.easing);
    }

    // ── Temporal bezier evaluation (scalar only) ─────────────────────────
    // Uses Newton-Raphson to solve x(u) = t, then returns y(u).
    [[nodiscard]] static T eval_temporal_bezier(
        const Keyframe<T>& prev, const Keyframe<T>& next, f32 t)
    {
        const f32 duration = static_cast<f32>(next.frame - prev.frame);
        if (duration <= 0.0f) return next.value;

        const f32 p0y = static_cast<f32>(prev.value);
        const f32 p3y = static_cast<f32>(next.value);

        // Out-tangent from prev keyframe
        const f32 p1x = std::clamp(prev.temporal_out_dx / duration, 0.0f, 1.0f);
        const f32 p1y = p0y + prev.temporal_out_dy;

        // In-tangent from next keyframe
        const f32 p2x = std::clamp(1.0f + next.temporal_in_dx / duration, 0.0f, 1.0f);
        const f32 p2y = p3y + next.temporal_in_dy;

        // X coefficients
        const f32 cx = 3.0f * p1x;
        const f32 bx = 3.0f * (p2x - p1x) - cx;
        const f32 ax = 1.0f - cx - bx;

        // Y coefficients
        const f32 cy = 3.0f * (p1y - p0y);
        const f32 by = 3.0f * (p2y - 2.0f * p1y + p0y);
        const f32 ay = p3y - p0y - cy - by;

        auto sample_x = [&](f32 u) -> f32 {
            return ((ax * u + bx) * u + cx) * u;
        };
        auto sample_y = [&](f32 u) -> f32 {
            return ((ay * u + by) * u + cy) * u + p0y;
        };
        auto sample_dx = [&](f32 u) -> f32 {
            return (3.0f * ax * u + 2.0f * bx) * u + cx;
        };

        // Newton-Raphson
        f32 u = t;
        for (int i = 0; i < 8; ++i) {
            f32 x_val = sample_x(u) - t;
            if (std::abs(x_val) < 1e-6f)
                return static_cast<T>(sample_y(u));
            f32 dVal = sample_dx(u);
            if (std::abs(dVal) < 1e-6f) break;
            u -= x_val / dVal;
        }

        // Bisection fallback
        f32 lo = 0.0f, hi = 1.0f;
        u = t;
        for (int i = 0; i < 16; ++i) {
            f32 x_val = sample_x(u);
            if (std::abs(x_val - t) < 1e-6f)
                return static_cast<T>(sample_y(u));
            if (t > x_val) lo = u; else hi = u;
            u = (lo + hi) * 0.5f;
        }
        return static_cast<T>(sample_y(u));
    }

    // Shared cache-check logic (Frame + SampleTime)
    [[nodiscard]] bool should_cache_double(double frame) const {
        if (has_expression()) return true;
        if (m_keyframes.empty()) return false;

        const double start_f = static_cast<double>(m_keyframes.front().frame);
        const double end_f   = static_cast<double>(m_keyframes.back().frame);

        if (frame <= start_f || frame >= end_f) return false;
        return true;
    }

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

// ── KeyframeTrack<T> — backward-compatible alias for AnimatedValue<T> ───
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

} // namespace chronon3d
