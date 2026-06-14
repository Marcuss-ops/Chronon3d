#pragma once

#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/expression.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <type_traits>
#include <unordered_map>

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

template <typename T>
class AnimatedValue {
public:
    AnimatedValue() = default;
    explicit AnimatedValue(T default_value) : m_default_value(default_value) {}

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

    // Fluent alias for add_keyframe — enables chaining.
    AnimatedValue& key(Frame frame, const T& value, EasingCurve easing = EasingCurve{Easing::Linear}) {
        add_keyframe(frame, value, easing);
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
    void compute_roving() {
        if (!m_roving_dirty) return;
        if (m_keyframes.size() < 3) { m_roving_dirty = false; return; }

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
            } else if constexpr (requires(const T& a, const T& b) { glm::length(a - b); }) {
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

    // Preferred name in new code.
    [[nodiscard]] T value_at(Frame frame) const { return evaluate(frame); }

    // ── Sub-frame evaluation (SampleTime) ── the future ─────────────────────
    [[nodiscard]] T evaluate(SampleTime time) const {
        return evaluate(time, AnimationEvalContext{});
    }

    [[nodiscard]] T evaluate(SampleTime time, const AnimationEvalContext& ctx) const {
        const T base = evaluate_base_double(time.frame);

        if (!has_expression()) return base;

        if constexpr (std::is_same_v<T, f32>) {
            const double fps = (time.fps > 0.0) ? time.fps : 30.0;
            const double t = time.seconds;
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
        }
        return base;
    }

    // ── Legacy Frame evaluation (backward compatible) ────────────────────────
    [[nodiscard]] T evaluate(Frame frame) const {
        return evaluate(SampleTime::from_frame_int(frame));
    }

    [[nodiscard]] T evaluate(Frame frame, const AnimationEvalContext& ctx) const {
        return evaluate(SampleTime::from_frame_int(frame), ctx);
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
     * @brief Returns true if this value depends on time in *any* way — either
     * through keyframes OR through an expression.  Expressions like
     * `sin(time * 2)` have no keyframes but are time-dependent; callers that
     * decide whether to evaluate/recompute must use is_time_dependent(), not
     * is_animated().
     */
    [[nodiscard]] bool is_time_dependent() const {
        return is_animated() || has_expression();
    }

    /**
     * @brief Determines if evaluating this property at a specific frame is "expensive"
     * enough to justify caching.
     */
    [[nodiscard]] bool should_cache(Frame frame) const { return should_cache_double(static_cast<double>(frame)); }

    [[nodiscard]] bool should_cache(const SampleTime& time) const { return should_cache_double(time.frame); }

    /// Returns the time (frame) of the last keyframe, or Frame{0} if empty.
    /// Useful for determining when an animation has reached its terminal state.
    [[nodiscard]] Frame last_keyframe_time() const {
        if (m_keyframes.empty()) return Frame{0};
        return m_keyframes.back().frame;
    }

    void clear() { m_keyframes.clear(); m_roving_dirty = true; }

    /// Shift every keyframe forward (or backward) by offset frames.
    /// Keyframes are clamped at Frame{0} to prevent negative frame indices.
    void shift(Frame offset) {
        for (auto& kf : m_keyframes) {
            kf.frame = std::max(Frame{0}, kf.frame + offset);
        }
    }

private:
    // Core interpolation engine — works with double precision for sub-frame accuracy.
    [[nodiscard]] T evaluate_base_double(double frame) const {
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

        // Binary search keyframe segment with double precision
        size_t idx = 0;
        for (size_t i = 0; i + 1 < m_keyframes.size(); ++i) {
            if (static_cast<double>(m_keyframes[i + 1].frame) >= eval_frame) {
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
        return interpolate_values(prev.value, next.value, std::clamp(t, 0.0f, 1.0f), prev.easing);
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
    std::vector<Keyframe<T>> m_keyframes;
    LoopMode m_loop_mode{LoopMode::Hold};
    std::string m_expression;
    bool m_roving_dirty{false};
};

} // namespace chronon3d
