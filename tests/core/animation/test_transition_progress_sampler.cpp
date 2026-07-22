// ═══════════════════════════════════════════════════════════════════════════
// test_transition_progress_sampler.cpp — TRN-02 canonical timing tests
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/animation/transition/transition_progress_sampler.hpp>
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace chronon3d;

namespace {

// Helper: build a SampleTime at a given integer frame using a rational rate.
SampleTime at_frame(Frame f, FrameRate rate) {
    return SampleTime::from_frame_int(f, rate);
}

SampleTime at_frame(double f, FrameRate rate) {
    return SampleTime::from_frame(f, rate);
}

// Check that a sample is finite and within [0, 1].
void check_sanity(const TransitionSample& s) {
    CHECK(std::isfinite(s.linear_progress));
    CHECK(std::isfinite(s.eased_progress));
    CHECK(s.linear_progress >= 0.0f);
    CHECK(s.linear_progress <= 1.0f);
    CHECK(s.eased_progress >= 0.0f);
    CHECK(s.eased_progress <= 1.0f);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Endpoint and phase flags
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TransitionProgressSampler: before / active / after flags") {
    const FrameRate rate{30, 1};
    const SampleTime start = at_frame(10.0, rate);
    const SampleTime end   = at_frame(40.0, rate);
    const EasingCurve linear{Easing::Linear};

    {
        auto s = sample_transition(at_frame(5.0, rate), start, end, linear);
        CHECK(s.before);
        CHECK(!s.active);
        CHECK(!s.after);
        CHECK(s.linear_progress == doctest::Approx(0.0f));
        CHECK(s.eased_progress == doctest::Approx(0.0f));
    }

    {
        auto s = sample_transition(at_frame(10.0, rate), start, end, linear);
        CHECK(!s.before);
        CHECK(s.active);
        CHECK(!s.after);
        CHECK(s.linear_progress == doctest::Approx(0.0f));
        CHECK(s.eased_progress == doctest::Approx(0.0f));
    }

    {
        auto s = sample_transition(at_frame(25.0, rate), start, end, linear);
        CHECK(!s.before);
        CHECK(s.active);
        CHECK(!s.after);
        CHECK(s.linear_progress == doctest::Approx(0.5f));
        CHECK(s.eased_progress == doctest::Approx(0.5f));
    }

    {
        auto s = sample_transition(at_frame(39.999, rate), start, end, linear);
        CHECK(!s.before);
        CHECK(s.active);
        CHECK(!s.after);
        CHECK(s.linear_progress == doctest::Approx(0.99997f).epsilon(1e-5f));
    }

    {
        auto s = sample_transition(at_frame(40.0, rate), start, end, linear);
        CHECK(!s.before);
        CHECK(!s.active);
        CHECK(s.after);
        CHECK(s.linear_progress == doctest::Approx(1.0f));
        CHECK(s.eased_progress == doctest::Approx(1.0f));
    }

    {
        auto s = sample_transition(at_frame(50.0, rate), start, end, linear);
        CHECK(!s.before);
        CHECK(!s.active);
        CHECK(s.after);
        CHECK(s.linear_progress == doctest::Approx(1.0f));
        CHECK(s.eased_progress == doctest::Approx(1.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Zero-duration transition (cut)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TransitionProgressSampler: zero duration behaves as cut") {
    const FrameRate rate{30, 1};
    const SampleTime start = at_frame(10.0, rate);
    const SampleTime end   = at_frame(10.0, rate); // same as start
    const EasingCurve linear{Easing::Linear};

    {
        auto s = sample_transition(at_frame(9.0, rate), start, end, linear);
        CHECK(s.before);
        CHECK(!s.active);
        CHECK(!s.after);
        CHECK(s.linear_progress == doctest::Approx(0.0f));
    }

    {
        auto s = sample_transition(at_frame(10.0, rate), start, end, linear);
        CHECK(!s.before);
        CHECK(!s.active);
        CHECK(s.after);
        CHECK(s.linear_progress == doctest::Approx(1.0f));
    }

    {
        auto s = sample_transition(at_frame(11.0, rate), start, end, linear);
        CHECK(!s.before);
        CHECK(!s.active);
        CHECK(s.after);
        CHECK(s.linear_progress == doctest::Approx(1.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. One-frame duration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TransitionProgressSampler: one-frame duration") {
    const FrameRate rate{30, 1};
    const SampleTime start = at_frame(10.0, rate);
    const SampleTime end   = at_frame(11.0, rate);
    const EasingCurve linear{Easing::Linear};

    {
        auto s = sample_transition(at_frame(10.0, rate), start, end, linear);
        CHECK(s.active);
        CHECK(s.linear_progress == doctest::Approx(0.0f));
    }

    {
        auto s = sample_transition(at_frame(10.5, rate), start, end, linear);
        CHECK(s.active);
        CHECK(s.linear_progress == doctest::Approx(0.5f));
    }

    {
        auto s = sample_transition(at_frame(11.0, rate), start, end, linear);
        CHECK(s.after);
        CHECK(s.linear_progress == doctest::Approx(1.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Easing application and direction
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TransitionProgressSampler: InOutCubic at midpoint stays 0.5") {
    const FrameRate rate{30, 1};
    const SampleTime start = at_frame(0.0, rate);
    const SampleTime end   = at_frame(30.0, rate);
    const EasingCurve inout{Easing::InOutCubic};

    auto s = sample_transition(at_frame(15.0, rate), start, end, inout);
    CHECK(s.linear_progress == doctest::Approx(0.5f));
    CHECK(s.eased_progress == doctest::Approx(0.5f));
}

TEST_CASE("TransitionProgressSampler: Out direction inverts progress") {
    const FrameRate rate{30, 1};
    const SampleTime start = at_frame(0.0, rate);
    const SampleTime end   = at_frame(10.0, rate);
    const EasingCurve linear{Easing::Linear};

    {
        auto s = sample_transition(at_frame(0.0, rate), start, end, linear, TransitionProgressDirection::Out);
        CHECK(s.eased_progress == doctest::Approx(1.0f));
    }
    {
        auto s = sample_transition(at_frame(5.0, rate), start, end, linear, TransitionProgressDirection::Out);
        CHECK(s.eased_progress == doctest::Approx(0.5f));
    }
    {
        auto s = sample_transition(at_frame(10.0, rate), start, end, linear, TransitionProgressDirection::Out);
        CHECK(s.eased_progress == doctest::Approx(0.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Monotonicity of easing
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TransitionProgressSampler: linear progress is monotonic") {
    const FrameRate rate{30, 1};
    const SampleTime start = at_frame(0.0, rate);
    const SampleTime end   = at_frame(30.0, rate);
    const EasingCurve linear{Easing::Linear};

    float prev = -1.0f;
    for (double f = 0.0; f <= 30.0; f += 1.0) {
        auto s = sample_transition(at_frame(f, rate), start, end, linear);
        check_sanity(s);
        CHECK(s.eased_progress >= prev);
        prev = s.eased_progress;
    }
}

TEST_CASE("TransitionProgressSampler: OutCubic progress is monotonic") {
    const FrameRate rate{30, 1};
    const SampleTime start = at_frame(0.0, rate);
    const SampleTime end   = at_frame(30.0, rate);
    const EasingCurve ease{Easing::OutCubic};

    float prev = -1.0f;
    for (double f = 0.0; f <= 30.0; f += 1.0) {
        auto s = sample_transition(at_frame(f, rate), start, end, ease);
        check_sanity(s);
        CHECK(s.eased_progress >= prev);
        prev = s.eased_progress;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Frame-rate independence
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TransitionProgressSampler: standard broadcast frame rates give same normalized progress") {
    const std::vector<FrameRate> rates = {
        FrameRate{24000, 1001}, // 23.976
        FrameRate{24, 1},
        FrameRate{25, 1},
        FrameRate{30000, 1001}, // 29.97
        FrameRate{30, 1},
        FrameRate{50, 1},
        FrameRate{60, 1},
    };

    for (const auto& rate : rates) {
        const SampleTime start = at_frame(0.0, rate);
        const SampleTime end   = at_frame(100.0, rate);
        const SampleTime mid   = at_frame(50.0, rate);
        const EasingCurve linear{Easing::Linear};

        auto s = sample_transition(mid, start, end, linear);
        check_sanity(s);
        CHECK(s.active);
        CHECK(s.linear_progress == doctest::Approx(0.5f).epsilon(1e-6f));
        CHECK(s.eased_progress == doctest::Approx(0.5f).epsilon(1e-6f));

        // Endpoint exactness at this rate.
        auto s0 = sample_transition(start, start, end, linear);
        CHECK(s0.linear_progress == doctest::Approx(0.0f));
        CHECK(s0.eased_progress == doctest::Approx(0.0f));

        auto s1 = sample_transition(end, start, end, linear);
        CHECK(s1.linear_progress == doctest::Approx(1.0f));
        CHECK(s1.eased_progress == doctest::Approx(1.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Sub-frame sampling
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TransitionProgressSampler: sub-frame samples are deterministic and distinct") {
    const FrameRate rate{30, 1};
    const SampleTime start = at_frame(0.0, rate);
    const SampleTime end   = at_frame(1.0, rate); // 1 frame duration
    const EasingCurve linear{Easing::Linear};

    std::vector<float> values;
    for (int i = 0; i <= 4; ++i) {
        double f = 0.0 + i * 0.25;
        auto s = sample_transition(at_frame(f, rate), start, end, linear);
        check_sanity(s);
        values.push_back(s.eased_progress);
    }

    // Distinct sub-frame values.
    for (size_t i = 1; i < values.size(); ++i) {
        CHECK(values[i] > values[i - 1]);
    }

    CHECK(values.front() == doctest::Approx(0.0f));
    CHECK(values.back() == doctest::Approx(1.0f));
}
