#pragma once

#include <chronon3d/core/types/time.hpp>

#include <limits>
#include <stdexcept>

namespace chronon3d::audio {

/// Exact half-open sample interval assigned to one output frame.
struct AudioSampleSpan {
    i64 first_sample{0};
    i64 sample_count{0};

    [[nodiscard]] constexpr i64 end_sample() const noexcept {
        return first_sample + sample_count;
    }

    constexpr bool operator==(const AudioSampleSpan&) const = default;
};

/// Maps rational output-frame time to integer audio-sample boundaries without
/// accumulating floating-point error.
///
/// For frame F, the boundary is:
///   floor(F * sample_rate * frame_rate.denominator / frame_rate.numerator)
///
/// Consecutive frame spans therefore partition the sample timeline exactly.
/// Fractional samples-per-frame rates naturally alternate their integer span
/// sizes while the cumulative boundary remains locked to the rational clock.
class AudioSampleClock final {
public:
    AudioSampleClock(i32 sample_rate, FrameRate frame_rate)
        : m_sample_rate(sample_rate), m_frame_rate(frame_rate) {
        if (m_sample_rate <= 0) {
            throw std::invalid_argument("audio sample rate must be positive");
        }
        if (m_frame_rate.numerator <= 0 || m_frame_rate.denominator <= 0) {
            throw std::invalid_argument("audio sample clock frame rate must be positive");
        }
    }

    [[nodiscard]] constexpr i32 sample_rate() const noexcept {
        return m_sample_rate;
    }

    [[nodiscard]] constexpr FrameRate frame_rate() const noexcept {
        return m_frame_rate;
    }

    /// Integer sample boundary at the beginning of `frame`.
    [[nodiscard]] i64 sample_index_at(Frame frame) const {
        return narrow_sample_index(sample_index_at_wide(frame.integral()));
    }

    /// Number of samples assigned to one frame. At fractional ratios this is
    /// intentionally not constant; the boundary difference is the authority.
    [[nodiscard]] i64 samples_for_frame(Frame frame) const {
        const __int128 frame_index = static_cast<__int128>(frame.integral());
        const __int128 first = sample_index_at_wide(frame_index);
        const __int128 last = sample_index_at_wide(frame_index + 1);
        return narrow_sample_count(last - first);
    }

    [[nodiscard]] AudioSampleSpan span_for_frame(Frame frame) const {
        const __int128 frame_index = static_cast<__int128>(frame.integral());
        const __int128 first = sample_index_at_wide(frame_index);
        const __int128 last = sample_index_at_wide(frame_index + 1);
        return AudioSampleSpan{
            .first_sample = narrow_sample_index(first),
            .sample_count = narrow_sample_count(last - first),
        };
    }

    /// Exact media timestamp for an integer sample boundary.
    [[nodiscard]] constexpr RationalTime sample_time(i64 sample_index) const noexcept {
        return RationalTime{sample_index, Rational{1, m_sample_rate}};
    }

private:
    [[nodiscard]] __int128 sample_index_at_wide(__int128 frame_index) const noexcept {
        // i64 frame * i32 sample-rate * i32 frame denominator fits signed
        // 128-bit range, so the exact product is safe before division.
        const __int128 scaled = frame_index *
                                static_cast<__int128>(m_sample_rate) *
                                static_cast<__int128>(m_frame_rate.denominator);
        return floor_div(scaled, static_cast<__int128>(m_frame_rate.numerator));
    }

    [[nodiscard]] static constexpr __int128 floor_div(
        __int128 numerator,
        __int128 positive_denominator) noexcept {
        const __int128 quotient = numerator / positive_denominator;
        const __int128 remainder = numerator % positive_denominator;
        return remainder != 0 && numerator < 0 ? quotient - 1 : quotient;
    }

    [[nodiscard]] static i64 narrow_sample_index(__int128 value) {
        if (value < static_cast<__int128>(std::numeric_limits<i64>::min()) ||
            value > static_cast<__int128>(std::numeric_limits<i64>::max())) {
            throw std::overflow_error("audio sample index overflow");
        }
        return static_cast<i64>(value);
    }

    [[nodiscard]] static i64 narrow_sample_count(__int128 value) {
        if (value < 0 ||
            value > static_cast<__int128>(std::numeric_limits<i64>::max())) {
            throw std::overflow_error("audio sample count overflow");
        }
        return static_cast<i64>(value);
    }

    i32 m_sample_rate{48000};
    FrameRate m_frame_rate{30, 1};
};

} // namespace chronon3d::audio
