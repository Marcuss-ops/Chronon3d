#pragma once

#include <chronon3d/core/types/types.hpp>
#include <compare>
#include <ostream>

namespace chronon3d {

/**
 * Represents a discrete point in time (frame number).
 *
 * Strong type: a Frame is a point, not a raw i64.
 * Prevents accidental mixing with widths, heights, byte counts, etc.
 *
 * Implicitly constructible from i64 for backward compatibility with
 * existing code. Explicit conversion operators are provided for
 * conversion to numeric types.
 *
 * ## Reading convention (pick ONE per context)
 *
 * | Context                          | Preferred form              | Notes                                  |
 * |----------------------------------|-----------------------------|----------------------------------------|
 * | Tests, logs, human-readable fmt  | `frame.integral()`          | Self-documenting, named accessor       |
 * | Interfacing with external APIs   | `static_cast<int>(frame)`   | Explicit narrowing, visible at call    |
 * | Core / time-critical code        | `frame.integral()`          | Layout-privacy invariant (see gate)    |
 *
 * **Note** (since 2026-07-08, enforced by
 * [`tools/check_frame_value_convention.sh`](../../../tools/check_frame_value_convention.sh)):
 * the historic `frame.value` direct-field-access concession for core
 * math / time-critical code is **SUPERSEDED** by strict gate enforcement.
 * The `.value` field is the Frame layout implementation detail; callers
 * MUST go through accessor methods (or `static_cast<int64_t>()` /
 * `Frame{N}+Frame{M}` arithmetic) so a future Frame layout change
 * (e.g. int width + sequence id + reserved fields) does not silently
 * break the caller. The strict gate makes `.value` access outside this
 * canonical header a deterministic link-time / compile-time violation.
 *
 * **Avoid** `frame.value` in any code (including core math / render code)
 * — use `frame.integral()` (self-documenting, named accessor, noexcept,
 * constexpr, fully inlinable; zero perf regression vs direct field
 * access). The `static_cast<int64_t>(frame)` form is permitted for
 * external-API narrowing only.
 *
 * **Avoid** `frame.value` in comments and documentation —
 * the gate scans both code AND comments for the pattern.
 */
struct Frame {
    i64 value{0};

    // ── Construction ────────────────────────────────────────────────────
    constexpr Frame() = default;
    /*implicit*/ constexpr Frame(i64 v) : value(v) {}

    // ── Comparison ──────────────────────────────────────────────────────
    auto operator<=>(const Frame&) const = default;

    // ── Arithmetic (Frame ± Frame) ──────────────────────────────────────
    constexpr Frame  operator+(Frame other) const { return Frame{value + other.value}; }
    constexpr Frame  operator-(Frame other) const { return Frame{value - other.value}; }
    constexpr Frame& operator+=(Frame other) { value += other.value; return *this; }
    constexpr Frame& operator-=(Frame other) { value -= other.value; return *this; }

    // ── Increment / decrement ───────────────────────────────────────────
    constexpr Frame& operator++()    { ++value; return *this; }
    constexpr Frame  operator++(int) { Frame tmp = *this; ++value; return tmp; }
    constexpr Frame& operator--()    { --value; return *this; }
    constexpr Frame  operator--(int) { Frame tmp = *this; --value; return tmp; }

    // ── Explicit conversions (prevents accidental use as width/height) ──
    constexpr explicit operator i64()    const { return value; }
    constexpr explicit operator u64()    const { return static_cast<u64>(value); }
    constexpr explicit operator f32()    const { return static_cast<f32>(value); }
    constexpr explicit operator double() const { return static_cast<double>(value); }
    constexpr explicit operator int()    const { return static_cast<int>(value); }

    // ── Named accessor (preferred over static_cast for readability) ────
    [[nodiscard]] i64 as_i64() const { return value; }
    [[nodiscard]] f32 as_f32() const { return static_cast<f32>(value); }
    [[nodiscard]] double as_double() const { return static_cast<double>(value); }
    /// Alias for `as_i64()` — matches the math convention where
    /// `integral()` denotes "round-trip to the underlying integer".
    /// Authoring fixtures and DSL wrappers prefer this name.
    [[nodiscard]] i64 integral() const noexcept { return value; }
};

// Free-function arithmetic for symmetry.
constexpr inline Frame operator+(i64 n, Frame f) { return Frame{n + f.value}; }
constexpr inline Frame operator-(i64 n, Frame f) { return Frame{n - f.value}; }

// ── Multiplication / division / modulo by scalar ───────────────────────
constexpr inline Frame operator*(Frame f, i64 n) { return Frame{f.value * n}; }
constexpr inline Frame operator*(i64 n, Frame f) { return Frame{n * f.value}; }
constexpr inline Frame operator/(Frame f, i64 n) { return Frame{f.value / n}; }
constexpr inline Frame operator%(Frame f, i64 n) { return Frame{f.value % n}; }

// ── Frame-on-Frame modulo (video_source.hpp needs frame % duration) ────
constexpr inline Frame operator%(Frame a, Frame b) { return Frame{a.value % b.value}; }

// ── Stream output (not constexpr — uses ostream) ───────────────────────
inline std::ostream& operator<<(std::ostream& os, Frame f) {
    return os << f.value;
}

} // namespace chronon3d

// ── fmt formatter specialization (must be at global/::fmt scope) ─────────
#include <fmt/format.h>

template <>
struct fmt::formatter<chronon3d::Frame> : fmt::formatter<int64_t> {
    auto format(chronon3d::Frame f, fmt::format_context& ctx) const {
        return fmt::formatter<int64_t>::format(f.value, ctx);
    }
};
