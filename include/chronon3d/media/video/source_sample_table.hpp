#pragma once

#include <chronon3d/core/types/time.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace chronon3d::media {

/// One presentation sample from the source stream. Timestamps and durations
/// are expressed in SourceSampleTable::time_base(); source_order is the
/// deterministic demux order used to break duplicate-PTS ties.
struct SourceSample {
    i64 pts{0};
    i64 duration{0};
    i64 dts{0};
    std::uint64_t source_order{0};
    std::uint32_t continuity_id{0};
    bool keyframe{false};

    constexpr bool operator==(const SourceSample&) const = default;
};

/// Canonical PTS-native resolver for source video samples.
///
/// Selection is exact and never consults nominal/average FPS:
/// presentation_time -> sample table -> sample whose [PTS, PTS+duration)
/// interval covers the requested time. Duplicate PTS values use source_order
/// as a stable tie-break. Gaps return no sample. A continuity-id change is an
/// explicit seek/reset boundary.
class SourceSampleTable {
public:
    explicit SourceSampleTable(Rational time_base = {1, 1})
        : m_time_base(normalize_rational(time_base)) {}

    [[nodiscard]] Rational time_base() const noexcept { return m_time_base; }
    [[nodiscard]] bool empty() const noexcept { return m_samples.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_samples.size(); }
    [[nodiscard]] const SourceSample& operator[](std::size_t index) const {
        return m_samples.at(index);
    }
    [[nodiscard]] const std::vector<SourceSample>& samples() const noexcept {
        return m_samples;
    }

    void add(SourceSample sample) {
        if (m_finalized) {
            throw std::logic_error("cannot append to finalized source sample table");
        }
        if (sample.duration < 0) sample.duration = 0;
        m_samples.push_back(sample);
    }

    /// Normalize ordering and fill missing packet durations from neighboring
    /// PTS values in the same continuity segment. No FPS estimate is used.
    void finalize() {
        if (m_finalized) return;
        std::stable_sort(m_samples.begin(), m_samples.end(), [](const SourceSample& a,
                                                               const SourceSample& b) {
            if (a.pts != b.pts) return a.pts < b.pts;
            return a.source_order < b.source_order;
        });

        for (std::size_t i = 0; i < m_samples.size(); ++i) {
            auto& sample = m_samples[i];
            if (sample.duration > 0) continue;

            for (std::size_t j = i + 1; j < m_samples.size(); ++j) {
                const auto& next = m_samples[j];
                if (next.continuity_id != sample.continuity_id) continue;
                if (next.pts > sample.pts) {
                    sample.duration = next.pts - sample.pts;
                    break;
                }
            }
            if (sample.duration > 0) continue;

            for (std::size_t j = i; j > 0; --j) {
                const auto& previous = m_samples[j - 1];
                if (previous.continuity_id == sample.continuity_id &&
                    previous.duration > 0) {
                    sample.duration = previous.duration;
                    break;
                }
            }
            // A single source time-base tick is the only final fallback. It
            // keeps the table exact without inventing a nominal frame rate.
            if (sample.duration <= 0) sample.duration = 1;
        }
        m_finalized = true;
    }

    [[nodiscard]] std::optional<std::size_t> select_covering(
        RationalTime presentation_time) const {
        require_finalized();
        if (m_samples.empty()) return std::nullopt;

        const auto upper = std::upper_bound(
            m_samples.begin(), m_samples.end(), presentation_time,
            [this](const RationalTime& time, const SourceSample& sample) {
                return compare(time, sample.pts) < 0;
            });
        if (upper == m_samples.begin()) return std::nullopt;

        std::size_t candidate = static_cast<std::size_t>(upper - m_samples.begin() - 1);
        const i64 candidate_pts = m_samples[candidate].pts;
        while (candidate > 0 && m_samples[candidate - 1].pts == candidate_pts) {
            --candidate;
        }

        for (std::size_t i = candidate;
             i < m_samples.size() && m_samples[i].pts == candidate_pts; ++i) {
            const auto& sample = m_samples[i];
            if (compare(presentation_time, sample.pts) >= 0 &&
                compare(presentation_time, checked_end(sample)) < 0) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool are_sequential(std::size_t previous,
                                      std::size_t next) const noexcept {
        if (previous >= m_samples.size() || next >= m_samples.size()) return false;
        return next == previous + 1 &&
               m_samples[previous].continuity_id == m_samples[next].continuity_id;
    }

    [[nodiscard]] std::optional<std::size_t> previous_keyframe(
        std::size_t sample_index) const {
        require_finalized();
        if (sample_index >= m_samples.size()) return std::nullopt;
        const auto continuity = m_samples[sample_index].continuity_id;
        for (std::size_t i = sample_index + 1; i > 0; --i) {
            const std::size_t candidate = i - 1;
            if (m_samples[candidate].continuity_id == continuity &&
                m_samples[candidate].keyframe) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    /// Zero-based occurrence of this PTS inside its continuity segment. Used
    /// after a keyframe seek to disambiguate duplicate presentation stamps.
    [[nodiscard]] std::size_t pts_ordinal(std::size_t sample_index) const {
        require_finalized();
        if (sample_index >= m_samples.size()) {
            throw std::out_of_range("source sample index out of range");
        }
        const auto& target = m_samples[sample_index];
        std::size_t ordinal = 0;
        for (std::size_t i = 0; i < sample_index; ++i) {
            if (m_samples[i].pts == target.pts &&
                m_samples[i].continuity_id == target.continuity_id) {
                ++ordinal;
            }
        }
        return ordinal;
    }

private:
    Rational m_time_base{1, 1};
    std::vector<SourceSample> m_samples;
    bool m_finalized{false};

    void require_finalized() const {
        if (!m_finalized) {
            throw std::logic_error("source sample table must be finalized before lookup");
        }
    }

    [[nodiscard]] i64 checked_end(const SourceSample& sample) const noexcept {
        const __int128 end = static_cast<__int128>(sample.pts) +
                             static_cast<__int128>(sample.duration);
        if (end > static_cast<__int128>(std::numeric_limits<i64>::max())) {
            return std::numeric_limits<i64>::max();
        }
        if (end < static_cast<__int128>(std::numeric_limits<i64>::min())) {
            return std::numeric_limits<i64>::min();
        }
        return static_cast<i64>(end);
    }

    [[nodiscard]] int compare(RationalTime lhs, i64 rhs_ticks) const {
        const Rational lhs_base = normalize_rational(lhs.time_base);
        const __int128 left = static_cast<__int128>(lhs.value) *
                              static_cast<__int128>(lhs_base.numerator) *
                              static_cast<__int128>(m_time_base.denominator);
        const __int128 right = static_cast<__int128>(rhs_ticks) *
                               static_cast<__int128>(m_time_base.numerator) *
                               static_cast<__int128>(lhs_base.denominator);
        if (left < right) return -1;
        if (left > right) return 1;
        return 0;
    }
};

} // namespace chronon3d::media
