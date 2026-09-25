#pragma once

// ── PointTracker2D — headless 2D point tracker ──────────────────────────────
//
// Tracks a patch across a sequence of RGBA frames by normalized
// cross-correlation (NCC) over luminance, with parabolic sub-pixel
// refinement.  It is deterministic: same frames, same start point, same
// track — there is no RNG and no wall clock anywhere in this path.
//
// The tracker emits plain samples (frame index, position, confidence); it
// owns no timeline vocabulary.  Turning samples into motion keyframes is the
// consumer's job (ChrononMotion3D's `motion/TrackedMotion.hpp` ingests them),
// so this type stays a pure pixel-analysis utility.

#include <chronon3d/math/color.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace chronon3d::media::tracking {

struct TrackerSample {
    std::uint32_t frame{0};
    float x{0.0f};
    float y{0.0f};
    float confidence{0.0f}; // NCC in [-1, 1] after sub-pixel refinement
    bool tracked{false};    // false when the patch was lost this frame
};

struct PointTrackSpec {
    int patch_radius{8};      // patch half-size in pixels [2, 64]
    int search_radius{24};    // per-frame search half-size in pixels [1, 256]
    float min_confidence{0.25f}; // NCC below this reports the sample lost
};

struct PointTrack {
    std::vector<TrackerSample> samples{};
    std::uint32_t lost_frames{0};

    [[nodiscard]] bool ok() const noexcept {
        return !samples.empty() && lost_frames == 0;
    }
};

class PointTracker2D {
public:
    PointTracker2D(int width, int height, PointTrackSpec spec = {})
        : m_width(width), m_height(height), m_spec(spec) {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("point tracker: frame dimensions must be positive");
        if (spec.patch_radius < 2 || spec.patch_radius > 64 ||
            spec.search_radius < 1 || spec.search_radius > 256 ||
            !std::isfinite(spec.min_confidence) || spec.min_confidence < 0.0f ||
            spec.min_confidence > 1.0f)
            throw std::invalid_argument("point tracker: spec is outside supported bounds");
    }

    // Anchors the track at (x, y) on `frame` and captures the template patch.
    TrackerSample start(std::span<const Color> frame, float x, float y,
                        std::uint32_t frame_index = 0) {
        check_frame(frame);
        if (!std::isfinite(x) || !std::isfinite(y))
            throw std::invalid_argument("point tracker: start position must be finite");
        m_x = x;
        m_y = y;
        capture_template(frame);
        m_has_position = true;
        TrackerSample sample{frame_index, x, y, 1.0f, true};
        m_track.samples.push_back(sample);
        return sample;
    }

    // Tracks the patch into the next frame.  On a lost patch the position is
    // held and `tracked` is false — a lost sample never injects a jump.
    TrackerSample push(std::span<const Color> frame, std::uint32_t frame_index) {
        check_frame(frame);
        if (!m_has_position)
            throw std::logic_error("point tracker: start() must be called before push()");
        float best_x = m_x, best_y = m_y;
        float best_score = -1.0f;
        const int search = m_spec.search_radius;
        const int base_x = static_cast<int>(std::lround(m_x));
        const int base_y = static_cast<int>(std::lround(m_y));
        for (int dy = -search; dy <= search; ++dy) {
            for (int dx = -search; dx <= search; ++dx) {
                const float score = ncc(frame, base_x + dx, base_y + dy);
                if (score > best_score) {
                    best_score = score;
                    best_x = static_cast<float>(base_x + dx);
                    best_y = static_cast<float>(base_y + dy);
                }
            }
        }
        // Parabolic sub-pixel refinement on the NCC surface.
        if (best_score > -1.0f) {
            const float sx = ncc(frame, static_cast<int>(best_x) - 1, static_cast<int>(best_y));
            const float sx1 = ncc(frame, static_cast<int>(best_x) + 1, static_cast<int>(best_y));
            const float sy = ncc(frame, static_cast<int>(best_x), static_cast<int>(best_y) - 1);
            const float sy1 = ncc(frame, static_cast<int>(best_x), static_cast<int>(best_y) + 1);
            best_x += parabolic_offset(sx, best_score, sx1);
            best_y += parabolic_offset(sy, best_score, sy1);
        }
        const float confidence = best_score > -1.0f ? best_score : 0.0f;
        const bool tracked = confidence >= m_spec.min_confidence;
        TrackerSample sample{frame_index, tracked ? best_x : m_x,
                             tracked ? best_y : m_y, confidence, tracked};
        if (tracked) {
            m_x = best_x;
            m_y = best_y;
            capture_template(frame);
        } else {
            ++m_track.lost_frames;
        }
        m_track.samples.push_back(sample);
        return sample;
    }

    [[nodiscard]] const PointTrack& track() const noexcept { return m_track; }
    [[nodiscard]] PointTrack take_track() noexcept { return std::move(m_track); }

private:
    static constexpr float kLumaR = 0.2126f;
    static constexpr float kLumaG = 0.7152f;
    static constexpr float kLumaB = 0.0722f;

    void check_frame(std::span<const Color> frame) const {
        if (frame.size() !=
            static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height))
            throw std::invalid_argument("point tracker: frame size does not match dimensions");
    }

    static float luma(const Color& c) noexcept {
        return kLumaR * c.r + kLumaG * c.g + kLumaB * c.b;
    }

    float sample_luma(std::span<const Color> frame, int x, int y) const noexcept {
        const int cx = std::clamp(x, 0, m_width - 1);
        const int cy = std::clamp(y, 0, m_height - 1);
        return luma(frame[static_cast<std::size_t>(cy) * m_width + cx]);
    }

    void capture_template(std::span<const Color> frame) {
        const int r = m_spec.patch_radius;
        const int side = 2 * r + 1;
        m_template.resize(static_cast<std::size_t>(side) * side);
        m_template_mean = 0.0f;
        const int cx = static_cast<int>(std::lround(m_x));
        const int cy = static_cast<int>(std::lround(m_y));
        for (int j = -r, k = 0; j <= r; ++j) {
            for (int i = -r; i <= r; ++i, ++k) {
                const float value = sample_luma(frame, cx + i, cy + j);
                m_template[static_cast<std::size_t>(k)] = value;
                m_template_mean += value;
            }
        }
        m_template_mean /= static_cast<float>(m_template.size());
    }

    // Normalized cross-correlation of the template against the patch centered
    // at (cx, cy).  Returns -1 when either patch is constant (no correlation
    // possible), which can never beat a real match.
    float ncc(std::span<const Color> frame, int cx, int cy) const noexcept {
        const int r = m_spec.patch_radius;
        float candidate_mean = 0.0f;
        for (int j = -r, k = 0; j <= r; ++j)
            for (int i = -r; i <= r; ++i, ++k)
                candidate_mean += sample_luma(frame, cx + i, cy + j);
        candidate_mean /= static_cast<float>(m_template.size());
        float numerator = 0.0f, denom_t = 0.0f, denom_c = 0.0f;
        for (int j = -r, k = 0; j <= r; ++j) {
            for (int i = -r; i <= r; ++i, ++k) {
                const float t = m_template[static_cast<std::size_t>(k)] - m_template_mean;
                const float c = sample_luma(frame, cx + i, cy + j) - candidate_mean;
                numerator += t * c;
                denom_t += t * t;
                denom_c += c * c;
            }
        }
        const float denominator = std::sqrt(denom_t * denom_c);
        if (denominator <= 1.0e-12f) return -1.0f;
        return std::clamp(numerator / denominator, -1.0f, 1.0f);
    }

    static float parabolic_offset(float left, float center, float right) noexcept {
        const float denominator = left - 2.0f * center + right;
        if (std::abs(denominator) <= 1.0e-12f) return 0.0f;
        return std::clamp(0.5f * (left - right) / denominator, -1.0f, 1.0f);
    }

    int m_width{0};
    int m_height{0};
    PointTrackSpec m_spec{};
    float m_x{0.0f};
    float m_y{0.0f};
    bool m_has_position{false};
    std::vector<float> m_template{};
    float m_template_mean{0.0f};
    PointTrack m_track{};
};

} // namespace chronon3d::media::tracking
