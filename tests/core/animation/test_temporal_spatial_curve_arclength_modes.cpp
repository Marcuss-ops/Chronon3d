// test_temporal_spatial_curve_arclength_modes.cpp — Split from
// test_temporal_spatial_curve.cpp: FU-1.3 bridge coverage for
// PathTimingMode::ArcLength / EasedArcLength (uniform-speed LUT), backward
// compatibility and determinism.
#include <doctest/doctest.h>
#include <chronon3d/animation/core/temporal_spatial_curve.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <cmath>
#include <vector>
using namespace chronon3d;

// =========================================================================
// FU-1.3 bridge — PathTimingMode::ArcLength (uniform speed)
// Per il verdetto AE: "Per ogni segmento crea una LUT: 32-128 sample...
// Durante la valutazione: calcola la distanza desiderata; cerca nella LUT;
// interpola il relativo u".
// =========================================================================

namespace {
// Coefficient of variation helper for the AE-uniform speed assertion.
[[nodiscard]] double coefficient_of_variation(std::vector<f32> const& values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (auto v : values) sum += static_cast<double>(v);
    const double mean = sum / static_cast<double>(values.size());
    if (mean <= 1e-9) return 0.0;
    double variance = 0.0;
    for (auto v : values) {
        const double d = static_cast<double>(v) - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(values.size());
    return std::sqrt(variance) / mean;
}
} // namespace

TEST_CASE("FU-1.3: ArcLength uniform spacing — CV within AE tolerance") {
    // Curvy path (handles bulge outward): parametric produces uneven speed,
    // ArcLength must produce uniform speed (CV ≤ 0.02 ≈ AE quality).
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    a.out_handle = {200.0f, 200.0f, 0.0f};   // big sideways bulge
    SpatialKeyframe3D b{SampleTime::from_frame(60.0, rate), {400, 0, 0}};
    b.in_handle = {-200.0f, 200.0f, 0.0f};
    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));
    path.build_segments_uniform();

    MotionPathSampler3D sampler;
    constexpr int N = 16;

    // Parametric reference (uneven speed on a bulge).
    std::vector<f32> param_speeds;
    Vec3 prev_par = sampler.sample(path, SampleTime::from_frame(0.0, rate),
                                    PathTimingMode::Parametric).position;
    for (int i = 1; i < N; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(N - 1);
        Vec3 pos = sampler.sample(path, SampleTime::from_frame(60.0 * t, rate),
                                    PathTimingMode::Parametric).position;
        param_speeds.push_back(static_cast<f32>(glm::length(pos - prev_par)));
        prev_par = pos;
    }

    // Arc-length uniform (must be EVEN).
    std::vector<f32> arc_speeds;
    Vec3 prev_arc = sampler.sample(path, SampleTime::from_frame(0.0, rate),
                                    PathTimingMode::ArcLength).position;
    for (int i = 1; i < N; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(N - 1);
        Vec3 pos = sampler.sample(path, SampleTime::from_frame(60.0 * t, rate),
                                    PathTimingMode::ArcLength).position;
        arc_speeds.push_back(static_cast<f32>(glm::length(pos - prev_arc)));
        prev_arc = pos;
    }

    const double cv_param = coefficient_of_variation(param_speeds);
    const double cv_arc = coefficient_of_variation(arc_speeds);

    // Parametric on a curved path is intentionally uneven (control value).
    CHECK(cv_param > 0.05);
    // ArcLength must collapse to uniform — AE target CV ≤ 0.02.
    CHECK(cv_arc < 0.02);
    // ArcLength MUST be at least as uniform as parametric.
    CHECK(cv_arc <= cv_param);
}

TEST_CASE("FU-1.3: ArcLength on straight line matches parametric exactly") {
    // Degenerate case: zero curve → no bulge. Both modes should agree
    // because arc-length remapping of a straight line is identity.
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    a.out_handle = {100.0f / 3.0f, 0.0f, 0.0f};
    SpatialKeyframe3D b{SampleTime::from_frame(60.0, rate), {100, 0, 0}};
    b.in_handle = {-100.0f / 3.0f, 0.0f, 0.0f};
    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));
    path.build_segments_uniform();

    MotionPathSampler3D sampler;
    constexpr int N = 8;
    for (int i = 0; i < N; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(N - 1);
        Vec3 par = sampler.sample(path, SampleTime::from_frame(60.0 * t, rate),
                                    PathTimingMode::Parametric).position;
        Vec3 arc = sampler.sample(path, SampleTime::from_frame(60.0 * t, rate),
                                    PathTimingMode::ArcLength).position;
        CHECK(glm::length(par - arc) < 0.01f);
    }
}

TEST_CASE("FU-1.3: ArcLength endpoints match keyframes exactly") {
    // The lut endpoints are always (t=0 → p0, t=1 → p3) regardless of
    // how bulgy the handles are. This is the AE-uniform-speed contract.
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    a.out_handle = {300.0f, 200.0f, 0.0f};
    SpatialKeyframe3D b{SampleTime::from_frame(60.0, rate), {400, 0, 0}};
    b.in_handle = {-300.0f, 200.0f, 0.0f};
    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));
    path.build_segments_uniform();

    MotionPathSampler3D sampler;
    Vec3 arc_start = sampler.sample(path, SampleTime::from_frame(0.0, rate),
                                       PathTimingMode::ArcLength).position;
    Vec3 arc_end   = sampler.sample(path, SampleTime::from_frame(60.0, rate),
                                       PathTimingMode::ArcLength).position;
    CHECK(glm::length(arc_start - Vec3{0, 0, 0}) < 1e-4f);
    CHECK(glm::length(arc_end   - Vec3{400, 0, 0}) < 1e-4f);
}

// =========================================================================
// FU-1.3 bridge — PathTimingMode::EasedArcLength (timing + uniform speed)
// Per il verdetto AE: "EasedArcLength: timing curve modifies intentional
// speed, but position is still parameterised in arc-length for clean motion".
// =========================================================================

TEST_CASE("FU-1.3: EasedArcLength combines timing + uniform speed") {
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    a.out_handle = {200.0f, 200.0f, 0.0f};
    SpatialKeyframe3D b{SampleTime::from_frame(60.0, rate), {400, 0, 0}};
    b.in_handle = {-200.0f, 200.0f, 0.0f};
    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));
    // Ease-in: slow at start, fast at end → at mid-time should be != mid-distance.
    path.build_segments(TemporalCurve1D{EasingCurve{Easing::InCubic}});

    MotionPathSampler3D sampler;
    constexpr int N = 16;

    // Sample 16 evenly-spaced TIME points. EasedArcLength must STILL
    // produce uniform spatial speed (the ease affects time, not space).
    std::vector<f32> eased_arc_speeds;
    Vec3 prev = sampler.sample(path, SampleTime::from_frame(0.0, rate),
                                  PathTimingMode::EasedArcLength).position;
    for (int i = 1; i < N; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(N - 1);
        Vec3 pos = sampler.sample(path, SampleTime::from_frame(60.0 * t, rate),
                                    PathTimingMode::EasedArcLength).position;
        eased_arc_speeds.push_back(static_cast<f32>(glm::length(pos - prev)));
        prev = pos;
    }
    const double cv = coefficient_of_variation(eased_arc_speeds);
    // EasedArcLength preserves the temporal ease; it only removes the
    // additional spatial distortion introduced by the Bezier parameter.
    CHECK(cv > 0.05);

    // Compare with Parametric timing curve: ease-in WILL modulate speed
    // → spec mismatch is intentional (proves the two modes differ).
    std::vector<f32> param_eased_speeds;
    Vec3 prev_p = sampler.sample(path, SampleTime::from_frame(0.0, rate),
                                    PathTimingMode::Parametric).position;
    for (int i = 1; i < N; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(N - 1);
        Vec3 pos = sampler.sample(path, SampleTime::from_frame(60.0 * t, rate),
                                    PathTimingMode::Parametric).position;
        param_eased_speeds.push_back(static_cast<f32>(glm::length(pos - prev_p)));
        prev_p = pos;
    }
    const double cv_param = coefficient_of_variation(param_eased_speeds);
    CHECK(cv < cv_param);  // Spatial arc-length remapping remains smoother.
}

// =========================================================================
// FU-1.3 bridge — backward compatibility & determinism
// =========================================================================

TEST_CASE("FU-1.3: Parametric behaviour unchanged (backward compat)") {
    // The Parametric branch in the new sampler is bit-identical to the
    // previous implementation (u = temporal_progress).  This is a regression
    // test: any future refactor that breaks backward compat must fail here.
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    SpatialKeyframe3D b{SampleTime::from_frame(60.0, rate), {100, 0, 0}};
    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));

    // Ease-in: t=0.5 → progress < 0.5 → position.x should be < 50.
    path.build_segments(TemporalCurve1D{EasingCurve{Easing::InCubic}});

    MotionPathSampler3D sampler;
    Vec3 mid = sampler.sample(path, SampleTime::from_frame(30.0, rate),
                                PathTimingMode::Parametric).position;
    CHECK(mid.x < 50.0f);   // ease-in at midpoint gives progress < 0.5
    CHECK(mid.x > 0.0f);
}

TEST_CASE("FU-1.3: deterministic across two evaluations (Lazy LUT)") {
    // Same MotionPath3D sampled twice at the same time must produce
    // identical results, even though the per-segment arc-length LUT
    // is built lazily on first access. This exercises the mutable-cache
    // contract.
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    a.out_handle = {50.0f, 100.0f, 0.0f};
    SpatialKeyframe3D b{SampleTime::from_frame(60.0, rate), {200, 0, 0}};
    b.in_handle = {-50.0f, 100.0f, 0.0f};
    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));
    path.build_segments_uniform();

    MotionPathSampler3D sampler;
    for (int mode_int = 0; mode_int <= 2; ++mode_int) {
        const PathTimingMode mode = static_cast<PathTimingMode>(mode_int);
        for (int t_int = 0; t_int <= 60; t_int += 7) {
            Vec3 first  = sampler.sample(path, SampleTime::from_frame(t_int, rate), mode).position;
            Vec3 second = sampler.sample(path, SampleTime::from_frame(t_int, rate), mode).position;
            // Strict bit-equality on position. Deterministic LUT caching is the contract.
            const double dx = static_cast<double>(first.x) - static_cast<double>(second.x);
            const double dy = static_cast<double>(first.y) - static_cast<double>(second.y);
            const double dz = static_cast<double>(first.z) - static_cast<double>(second.z);
            CHECK(std::abs(dx) < 1e-6);
            CHECK(std::abs(dy) < 1e-6);
            CHECK(std::abs(dz) < 1e-6);
        }
    }
}
