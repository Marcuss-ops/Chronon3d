// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_trajectory.cpp
//
// Implements CameraTrajectory::sample() using per-segment sampler adapters.
//
// Segment samplers delegate to the project's existing, well-tested math:
//   - BezierTrajectorySampler   → SpatialBezierPath (2 waypoints)
//   - CatmullRomTrajectorySampler → standard Catmull-Rom formula
//     (same polynomial as CatmullRomPath3D, extracted here because the
//      path type doesn't expose single-segment sub-range evaluation)
//   - LinearTrajectorySampler   → direct lerp
//   - HoldTrajectorySampler     → static position
//
// Arc-length LUT is built by sampling ALL segments uniformly through the
// samplers' sample_positions(n) interface (which calls the actual curve
// evaluators — not just point-to-point straight-line distance).
//
// Sub-frame: CameraTrajectory::sample() reads ctx.sample_time.frame (double)
// instead of casting ctx.frame to int.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>

#include <chronon3d/animation/path/spatial_bezier_path.hpp>

#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace chronon3d::camera_v1 {

// =========================================================================
// Segment sampler implementations.
// =========================================================================

// --- Linear ------------------------------------------------------------------
class LinearTrajectorySampler final : public TrajectorySegmentSampler {
    Vec3 p0_, p1_;
    const CameraTrajectoryPoint* pt0_;
    const CameraTrajectoryPoint* pt1_;
public:
    LinearTrajectorySampler(const CameraTrajectoryPoint& a, const CameraTrajectoryPoint& b)
        : p0_(a.position), p1_(b.position), pt0_(&a), pt1_(&b) {}

    CameraTrajectorySample sample(float t01) const override {
        t01 = std::clamp(t01, 0.0f, 1.0f);
        Vec3 pos = { p0_.x + (p1_.x - p0_.x) * t01,
                     p0_.y + (p1_.y - p0_.y) * t01,
                     p0_.z + (p1_.z - p0_.z) * t01 };
        Vec3 dir = p1_ - p0_;
        float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        Vec3 tan = (len > 1e-6f) ? Vec3{dir.x/len, dir.y/len, dir.z/len} : Vec3{0,0,0};
        return { pos, tan,
                 pt1_->target ? pt1_->target : pt0_->target,
                 pt0_->roll_deg + (pt1_->roll_deg - pt0_->roll_deg) * t01 };
    }

    std::vector<Vec3> sample_positions(int n) const override {
        std::vector<Vec3> out(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(n - 1);
            t = std::clamp(t, 0.0f, 1.0f);
            out[static_cast<std::size_t>(i)] = {
                p0_.x + (p1_.x - p0_.x) * t,
                p0_.y + (p1_.y - p0_.y) * t,
                p0_.z + (p1_.z - p0_.z) * t };
        }
        return out;
    }
};

// --- Bézier ------------------------------------------------------------------
class BezierTrajectorySampler final : public TrajectorySegmentSampler {
    SpatialBezierPath path_;  // 2 waypoints = 1 cubic-bezier segment
    const CameraTrajectoryPoint* pt0_;
    const CameraTrajectoryPoint* pt1_;
public:
    BezierTrajectorySampler(const CameraTrajectoryPoint& p0, const CameraTrajectoryPoint& p1)
        : pt0_(&p0), pt1_(&p1) {
        // Waypoint 0: position = P0, out_handle = local offset to control point.
        // Waypoint 1: position = P3, in_handle  = local offset from control point.
        path_.add_waypoint(p0.position, p0.handle_out, {0,0,0});
        path_.add_waypoint(p1.position, {0,0,0}, p1.handle_in);
    }

    CameraTrajectorySample sample(float t01) const override {
        t01 = std::clamp(t01, 0.0f, 1.0f);
        Vec3 pos = path_.evaluate(t01);
        Vec3 raw = path_.tangent_at(t01);
        float len = glm::length(raw);
        Vec3 tan = (len > 1e-6f) ? (raw / len) : Vec3{0,0,0};
        return { pos, tan,
                 pt1_->target ? pt1_->target : pt0_->target,
                 pt0_->roll_deg + (pt1_->roll_deg - pt0_->roll_deg) * t01 };
    }

    std::vector<Vec3> sample_positions(int n) const override {
        std::vector<Vec3> out(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(n - 1);
            out[static_cast<std::size_t>(i)] = path_.evaluate(std::clamp(t, 0.0f, 1.0f));
        }
        return out;
    }
};

// --- Catmull-Rom -------------------------------------------------------------
// Uses the standard Catmull-Rom polynomial (identical to CatmullRomPath3D,
// extracted here because that type evaluates across its entire multi-segment
// path and doesn't expose single-segment sub-range sampling).
class CatmullRomTrajectorySampler final : public TrajectorySegmentSampler {
    Vec3 P0_, P1_, P2_, P3_;
    const CameraTrajectoryPoint* pt0_;
    const CameraTrajectoryPoint* pt1_;
public:
    CatmullRomTrajectorySampler(Vec3 p_prev,
                                 const CameraTrajectoryPoint& p0,
                                 const CameraTrajectoryPoint& p1,
                                 Vec3 p_next)
        : P0_(p_prev), P1_(p0.position), P2_(p1.position), P3_(p_next)
        , pt0_(&p0), pt1_(&p1) {}

    CameraTrajectorySample sample(float t01) const override {
        t01 = std::clamp(t01, 0.0f, 1.0f);
        Vec3 pos = catmull_rom(P0_, P1_, P2_, P3_, t01);
        Vec3 raw = catmull_rom_derivative(P0_, P1_, P2_, P3_, t01);
        float len = glm::length(raw);
        Vec3 tan = (len > 1e-6f) ? (raw / len) : Vec3{0,0,0};
        return { pos, tan,
                 pt1_->target ? pt1_->target : pt0_->target,
                 pt0_->roll_deg + (pt1_->roll_deg - pt0_->roll_deg) * t01 };
    }

    std::vector<Vec3> sample_positions(int n) const override {
        std::vector<Vec3> out(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(n - 1);
            out[static_cast<std::size_t>(i)] = catmull_rom(P0_, P1_, P2_, P3_, std::clamp(t, 0.0f, 1.0f));
        }
        return out;
    }

private:
    // Standard Catmull-Rom: passes through P1 at t=0 and P2 at t=1.
    static Vec3 catmull_rom(const Vec3& P0, const Vec3& P1,
                             const Vec3& P2, const Vec3& P3, float t) {
        float t2 = t * t;
        float t3 = t2 * t;
        return 0.5f * (
            (2.0f * P1) +
            (-P0 + P2) * t +
            (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t2 +
            (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t3
        );
    }

    static Vec3 catmull_rom_derivative(const Vec3& P0, const Vec3& P1,
                                        const Vec3& P2, const Vec3& P3, float t) {
        float t2 = t * t;
        return 0.5f * (
            (-P0 + P2) +
            2.0f * (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t +
            3.0f * (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t2
        );
    }
};

// --- Hold --------------------------------------------------------------------
class HoldTrajectorySampler final : public TrajectorySegmentSampler {
    Vec3 pos_;
    std::optional<Vec3> target_;
    float roll_deg_;
public:
    HoldTrajectorySampler(const CameraTrajectoryPoint& p)
        : pos_(p.position), target_(p.target), roll_deg_(p.roll_deg) {}

    CameraTrajectorySample sample(float /*t01*/) const override {
        return { pos_, {0,0,0}, target_, roll_deg_ };
    }

    std::vector<Vec3> sample_positions(int n) const override {
        return std::vector<Vec3>(static_cast<std::size_t>(n), pos_);
    }
};

// =========================================================================
// Segment sampler factory.
// =========================================================================
namespace {

std::unique_ptr<TrajectorySegmentSampler> make_sampler(
    SegmentKind kind,
    const std::vector<CameraTrajectoryPoint>& pts,
    std::size_t from_idx, std::size_t to_idx)
{
    if (from_idx >= pts.size() || to_idx >= pts.size())
        return nullptr;

    switch (kind) {
    case SegmentKind::Linear:
        return std::make_unique<LinearTrajectorySampler>(pts[from_idx], pts[to_idx]);

    case SegmentKind::CubicBezier:
        return std::make_unique<BezierTrajectorySampler>(pts[from_idx], pts[to_idx]);

    case SegmentKind::CatmullRom: {
        // Build 4 control points. For the first segment (from_idx==0), mirror P0.
        // For the last segment, mirror P_last.
        Vec3 p_prev = (from_idx > 0)
            ? pts[from_idx - 1].position
            : pts[from_idx].position + (pts[from_idx].position - pts[to_idx].position);
        Vec3 p_next = (to_idx + 1 < pts.size())
            ? pts[to_idx + 1].position
            : pts[to_idx].position + (pts[to_idx].position - pts[from_idx].position);
        return std::make_unique<CatmullRomTrajectorySampler>(
            p_prev, pts[from_idx], pts[to_idx], p_next);
    }

    case SegmentKind::Hold:
        return std::make_unique<HoldTrajectorySampler>(pts[from_idx]);
    }
    return nullptr;
}

// Build arc-length LUT by sampling every segment's actual curve.
std::shared_ptr<ArcLengthTable> build_arc_length_lut(
    const std::vector<std::unique_ptr<TrajectorySegmentSampler>>& samplers)
{
    constexpr int kSamplesPerSegment = 64;
    if (samplers.empty()) return nullptr;

    std::vector<ArcLengthEntry> entries;
    float cumulative = 0.0f;
    int total_segs = static_cast<int>(samplers.size());

    for (int seg = 0; seg < total_segs; ++seg) {
        auto positions = samplers[static_cast<std::size_t>(seg)]->sample_positions(kSamplesPerSegment);
        for (int i = 0; i < kSamplesPerSegment; ++i) {
            if (i > 0) {
                Vec3 delta = positions[static_cast<std::size_t>(i)]
                           - positions[static_cast<std::size_t>(i - 1)];
                cumulative += std::sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
            }
            float param = (static_cast<float>(seg) +
                          static_cast<float>(i) / static_cast<float>(kSamplesPerSegment - 1))
                         / static_cast<float>(total_segs);
            entries.push_back({cumulative, param});
        }
    }
    return std::make_shared<ArcLengthTable>(std::move(entries));
}

} // namespace

// =========================================================================
// CameraTrajectory::sample — sub-frame aware.
// =========================================================================

CameraTrajectorySample CameraTrajectory::sample(const CameraMotionContext& ctx) const {
    if (segments_.empty() || points_.empty() || samplers_.empty()) return {};

    // Sub-frame: use ctx.sample_time.frame (double) for continuous time.
    const double frame_position = ctx.sample_time.frame;
    const double total = static_cast<double>(total_frames());
    if (total <= 0.0) return samplers_.front()->sample(0.0f);

    // Clamp to [0, total] so frame_position 0 maps to t=0, total maps to t=1.
    const double clamped = std::clamp(frame_position, 0.0, total);

    // Find which segment contains frame_position.
    double cursor = 0.0;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        double seg_dur = static_cast<double>(segments_[i].duration_frames);
        double seg_end = cursor + seg_dur;
        bool is_last = (i + 1 == segments_.size());

        if (clamped <= seg_end || is_last) {
            double local = clamped - cursor;
            float t01 = static_cast<float>(local / std::max(1.0, seg_dur));
            t01 = std::clamp(t01, 0.0f, 1.0f);

            // Arc-length remap: convert frame fraction → global parametric t,
            // then extract the segment-local t01 for the current segment.
            // NOTE: This assumes uniform parametric segment width (1/N each).
            // When segment durations differ sharply from their arc-length
            // proportions, the time-based segment search (above) may pick a
            // different segment than the remapped global_t implies.
            // P13 will add per-segment mini-LUTs for a rigorous fix.
            if (pre_lut_) {
                float frame_frac = static_cast<float>(clamped / total);
                float global_t = pre_lut_->parameter_at_fraction(frame_frac);
                const float seg_width = 1.0f / static_cast<float>(segments_.size());
                const float seg_start = static_cast<float>(i) * seg_width;
                t01 = (seg_width > 0.0f)
                    ? (global_t - seg_start) / seg_width
                    : 0.0f;
                t01 = std::clamp(t01, 0.0f, 1.0f);
            }
            return samplers_[i]->sample(t01);
        }
        cursor = seg_end;
    }
    return samplers_.back()->sample(1.0f);
}

float CameraTrajectory::total_frames() const {
    float s = 0.0f;
    for (auto& sg : segments_) s += sg.duration_frames;
    return s;
}

// =========================================================================
// Builder
// =========================================================================

CameraTrajectoryBuilder& CameraTrajectoryBuilder::move_to(Vec3 p,
                                                          std::optional<Vec3> target,
                                                          float roll_deg) {
    pts_.push_back({ p, {0,0,0}, {0,0,0}, target, roll_deg });
    if (pts_.size() >= 2) {
        segs_.push_back({ SegmentKind::Linear,
                          pts_.size() - 2,
                          pts_.size() - 1,
                          30.0f });
    }
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::bezier_to(Vec3 handle_in,
                                                            Vec3 handle_out,
                                                            Vec3 to) {
    if (pts_.empty()) {
        pts_.push_back({ to, {0,0,0}, {0,0,0}, std::nullopt, 0.0f });
        return *this;
    }
    // handle_in, handle_out are LOCAL OFFSETS from the point's position
    // (matching SpatialBezierPath's Waypoint convention).
    pts_.push_back({ to, handle_in, handle_out, std::nullopt, 0.0f });
    segs_.push_back({ SegmentKind::CubicBezier,
                      pts_.size() - 2,
                      pts_.size() - 1,
                      30.0f });
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::catmull_rom_to(Vec3 to) {
    if (pts_.empty()) {
        pts_.push_back({ to, {0,0,0}, {0,0,0}, std::nullopt, 0.0f });
        return *this;
    }
    pts_.push_back({ to, {0,0,0}, {0,0,0}, std::nullopt, 0.0f });
    segs_.push_back({ SegmentKind::CatmullRom,
                      pts_.size() - 2,
                      pts_.size() - 1,
                      30.0f });
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::hold_for(float frames) {
    if (pts_.empty()) return *this;
    segs_.push_back({ SegmentKind::Hold,
                      pts_.size() - 1,
                      pts_.size() - 1,
                      std::max(0.0f, frames) });
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::duration_frames(float f) {
    if (segs_.empty()) return *this;
    segs_.back().duration_frames = std::max(1.0f, f);
    return *this;
}

CameraTrajectoryBuilder& CameraTrajectoryBuilder::arc_length_parameterized(bool on) {
    arc_len_ = on;
    return *this;
}

std::shared_ptr<CameraTrajectory> CameraTrajectoryBuilder::build() {
    if (pts_.size() < 2) {
        throw std::invalid_argument("CameraTrajectoryBuilder: need >= 2 points");
    }
    auto out = std::make_shared<CameraTrajectory>();
    out->points_   = std::move(pts_);
    out->segments_ = std::move(segs_);

    // Build per-segment samplers.
    out->samplers_.reserve(out->segments_.size());
    for (auto& sg : out->segments_) {
        auto s = make_sampler(sg.kind, out->points_, sg.from_idx, sg.to_idx);
        if (!s) throw std::logic_error("CameraTrajectoryBuilder: failed to create segment sampler");
        out->samplers_.push_back(std::move(s));
    }

    // Build arc-length LUT by sampling the real curves.
    if (arc_len_) {
        out->pre_lut_ = build_arc_length_lut(out->samplers_);
    }
    return out;
}

} // namespace chronon3d::camera_v1
