// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_trajectory.cpp
//
// Implements CameraTrajectory::sample() and segment evaluators.
//
// Layout:
//   - ArcLengthTable: pre-computed cumulative arc length LUT for O(1) sample
//     remap. Stored as std::vector<float> per-segment + cumulative total.
//   - linear_segment     : standard 2-point lerp.
//   - cubic_bezier_segment : de Casteljau at t01, handles in/out absolute world.
//   - catmull_rom_segment : uniform centripetal Catmull-Rom.
//   - hold_segment       : no movement, returns p0.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>

#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace chronon3d::camera_v1 {

// ---------------------------------------------------------------------------
// Segment evaluators (anonymous-namespace private).
// `ArcLengthTable` is defined in its own public header (no PIMPL needed).
// ---------------------------------------------------------------------------
namespace {

inline Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return { a.x + (b.x - a.x) * t,
             a.y + (b.y - a.y) * t,
             a.z + (b.z - a.z) * t };
}

inline Vec3 unit(Vec3 v) {
    float d = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (d < 1e-6f) return {0,0,0};
    return { v.x/d, v.y/d, v.z/d };
}

CameraTrajectorySample linear_segment(const CameraTrajectory& tr,
                                      const CameraTrajectorySegment& /*sg*/,
                                      float t01) {
    CameraTrajectorySample s;
    if (tr.points().size() < 2) return s;
    auto& p0 = tr.points().front();
    auto& p1 = tr.points().back();
    s.position = lerp(p0.position, p1.position, t01);
    s.tangent   = unit(p1.position - p0.position);
    s.target    = p1.target ? p1.target : p0.target;
    s.roll_deg  = p0.roll_deg + (p1.roll_deg - p0.roll_deg) * t01;
    return s;
}

CameraTrajectorySample cubic_bezier_segment(const CameraTrajectory& tr,
                                            const CameraTrajectorySegment& sg,
                                            float t01) {
    CameraTrajectorySample s;
    if (sg.from_idx + 1 > tr.points().size() || sg.to_idx > tr.points().size()) return s;
    auto& p0 = tr.points()[sg.from_idx];
    auto& p3 = tr.points()[sg.to_idx];
    Vec3 p1 = p0.position + p0.handle_out;       // abs-world handle_out
    Vec3 p2 = p3.position + p3.handle_in;        // abs-world handle_in
    float u = 1.0f - t01;
    auto q = [&](float a, float b, float c, float d) {
        return u*u*u*a + 3*u*u*t01*b + 3*u*t01*t01*c + t01*t01*t01*d;
    };
    s.position = { q(p0.position.x, p1.x, p2.x, p3.position.x),
                   q(p0.position.y, p1.y, p2.y, p3.position.y),
                   q(p0.position.z, p1.z, p2.z, p3.position.z) };
    // tangent: analytic derivative (3u² P0 + 6u(1-u) P1 + 3(1-u)² P2)
    float du = 3.0f * u * u;
    float dv = 6.0f * u * t01;
    float dw = 3.0f * t01 * t01;
    Vec3 tangent = {
        du * p0.position.x + dv * p1.x + dw * p2.x,
        du * p0.position.y + dv * p1.y + dw * p2.y,
        du * p0.position.z + dv * p1.z + dw * p2.z
    };
    s.tangent = unit(tangent);
    s.target  = p3.target ? p3.target : p0.target;
    s.roll_deg = p0.roll_deg + (p3.roll_deg - p0.roll_deg) * t01;
    return s;
}

CameraTrajectorySample catmull_rom_segment(const CameraTrajectory& tr,
                                           const CameraTrajectorySegment& sg,
                                           float t01) {
    CameraTrajectorySample s;
    if (sg.from_idx == 0 || sg.to_idx >= tr.points().size()) return linear_segment(tr, sg, t01);
    auto& pm1 = tr.points()[sg.from_idx - 1];
    auto& p0  = tr.points()[sg.from_idx];
    auto& p1  = tr.points()[sg.to_idx];
    auto& p2  = tr.points()[std::min(sg.to_idx + 1, tr.points().size() - 1)];
    float t2 = t01 * t01;
    float t3 = t2 * t01;
    auto basis = [&](float a, float b, float c, float d) {
        return 0.5f * ((2*b) + (-a + c)*t01 + (2*a - 5*b + 4*c - d)*t2 + (-a + 3*b - 3*c + d)*t3);
    };
    s.position = { basis(pm1.position.x, p0.position.x, p1.position.x, p2.position.x),
                   basis(pm1.position.y, p0.position.y, p1.position.y, p2.position.y),
                   basis(pm1.position.z, p0.position.z, p1.position.z, p2.position.z) };
    Vec3 tangent = {
        0.5f * ((-pm1.position.x + p1.position.x) + 2*(2*pm1.position.x - 5*p0.position.x + 4*p1.position.x - p2.position.x)*t01 + 3*(-pm1.position.x + 3*p0.position.x - 3*p1.position.x + p2.position.x)*t2),
        0.5f * ((-pm1.position.y + p1.position.y) + 2*(2*pm1.position.y - 5*p0.position.y + 4*p1.position.y - p2.position.y)*t01 + 3*(-pm1.position.y + 3*p0.position.y - 3*p1.position.y + p2.position.y)*t2),
        0.5f * ((-pm1.position.z + p1.position.z) + 2*(2*pm1.position.z - 5*p0.position.z + 4*p1.position.z - p2.position.z)*t01 + 3*(-pm1.position.z + 3*p0.position.z - 3*p1.position.z + p2.position.z)*t2)
    };
    s.tangent = unit(tangent);
    s.target = p1.target ? p1.target : p0.target;
    s.roll_deg = p0.roll_deg + (p1.roll_deg - p0.roll_deg) * t01;
    return s;
}

CameraTrajectorySample hold_segment(const CameraTrajectory& tr,
                                    const CameraTrajectorySegment& sg,
                                    float /*t01*/) {
    CameraTrajectorySample s;
    if (sg.from_idx >= tr.points().size()) return s;
    auto& p = tr.points()[sg.from_idx];
    s.position = p.position;
    s.tangent  = {0,0,0};
    s.target   = p.target;
    s.roll_deg = p.roll_deg;
    return s;
}

CameraTrajectorySample dispatch(const CameraTrajectory& tr,
                                const CameraTrajectorySegment& sg,
                                float t01) {
    switch (sg.kind) {
        case SegmentKind::Linear:     return linear_segment(tr, sg, t01);
        case SegmentKind::CubicBezier:return cubic_bezier_segment(tr, sg, t01);
        case SegmentKind::CatmullRom: return catmull_rom_segment(tr, sg, t01);
        case SegmentKind::Hold:       return hold_segment(tr, sg, t01);
    }
    return hold_segment(tr, sg, t01);
}

} // namespace

// ---------------------------------------------------------------------------
// Public accessor used by CameraTrajectory (friend).
// ---------------------------------------------------------------------------
CameraTrajectorySample sample_trajectory_segment(
        const CameraTrajectory& tr,
        const CameraTrajectorySegment& sg,
        float t01) {
    return dispatch(tr, sg, std::clamp(t01, 0.0f, 1.0f));
}

// ---------------------------------------------------------------------------
// Forwarded member definitions.
// ---------------------------------------------------------------------------

CameraTrajectorySample CameraTrajectory::sample(const CameraMotionContext& ctx) const {
    if (segments_.empty() || points_.empty()) return {};

    int frame = static_cast<int>(ctx.frame);
    int cursor = 0;
    for (auto& sg : segments_) {
        int seg_end = cursor + static_cast<int>(sg.duration_frames);
        if (frame <= seg_end || &sg == &segments_.back()) {
            int local = frame - cursor;
            float t01 = local / std::max(1.0f, sg.duration_frames);
            t01 = std::clamp(t01, 0.0f, 1.0f);
            if (pre_lut_) {
                float arc_t = pre_lut_->t_from_arc_length(t01);
                t01 = arc_t;
            }
            return sample_trajectory_segment(*this, sg, t01);
        }
        cursor = seg_end;
    }
    return sample_trajectory_segment(*this, segments_.back(), 1.0f);
}

float CameraTrajectory::total_frames() const {
    float s = 0.0f;
    for (auto& sg : segments_) s += sg.duration_frames;
    return s;
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

CameraTrajectoryBuilder& CameraTrajectoryBuilder::move_to(Vec3 p,
                                                          std::optional<Vec3> target,
                                                          float roll_deg) {
    pts_.push_back({ p, {0,0,0}, {0,0,0}, target, roll_deg });
    if (pts_.size() >= 2) {
        // auto-emit a linear segment to this new point.
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
                      pts_.size() - 1,   // same index
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
    out->points_  = pts_;
    out->segments_ = segs_;
    if (arc_len_) {
        // Approximate total arc length by sampling each segment uniformly.
        // Cheap and adequate for V1 velocity-uniform look; finer LUTs land in P13.
        std::vector<float> cum;
        float total = 0.0f;
        cum.push_back(0.0f);
        for (std::size_t i = 1; i < pts_.size(); ++i) {
            float dx = pts_[i].position.x - pts_[i-1].position.x;
            float dy = pts_[i].position.y - pts_[i-1].position.y;
            float dz = pts_[i].position.z - pts_[i-1].position.z;
            total += std::sqrt(dx*dx + dy*dy + dz*dz);
            cum.push_back(total);
        }
        out->pre_lut_ = std::make_shared<ArcLengthTable>(std::move(cum));
    }
    return out;
}

} // namespace chronon3d::camera_v1
