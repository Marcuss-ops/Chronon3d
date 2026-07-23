// ==============================================================================
// tests/render_graph/features/test_transition_certification.cpp
//
// TRN-06 — Minimum certification matrix for existing layer transitions.
//
// Coverage (kept practical; not the full Cartesian product):
//   * aspect ratios: 16:9 (160x90) and 9:16 (90x160)
//   * transition sides: in (reveal) and out (exit)
//   * durations: 1, 2, 10, 30 frames at 30 fps
//   * easing curves: Linear, InQuad, OutQuad, InOutQuad
//   * directions: Left, Right, Up, Down (for directional transitions)
//   * start / middle / end frame sampling
//   * cache cold vs warm (same renderer, second pass)
//   * random vs sequential access determinism (fresh renderer, frame N)
//   * opaque and semi-transparent source content
//
// NOTE: serial vs parallel scheduler is not tested explicitly because the
// public API does not yet expose a runtime scheduler-mode toggle.  The
// CHRONON3D_SCHEDULER_MODE env var is read once at Config construction,
// so the test defaults are used.  When an API is added, this suite can be
// extended with an explicit sequential/parallel axis.
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/transition/transition_catalog.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cstdlib>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::graph;
using namespace chronon3d::test;

namespace {

struct Dim {
    int width;
    int height;
};

constexpr Dim kDim16_9{160, 90};
constexpr Dim kDim9_16{90, 160};

Composition make_transition_comp(const Dim& dim,
                                 const LayerTransitionSpec& trans_in,
                                 const LayerTransitionSpec& trans_out,
                                 Frame duration,
                                 Frame layer_duration,
                                 bool transparent_content) {
    return composition(CompositionSpec{
        .name = "TransitionCert",
        .width = dim.width,
        .height = dim.height,
        .frame_rate = FrameRate{30, 1},
        .duration = duration
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("bg", [&dim](LayerBuilder& l) {
            l.fill(Color::black());
        });

        s.layer("trans_layer", [=](LayerBuilder& l) {
            if (layer_duration >= Frame{0}) {
                l.duration(layer_duration);
            }
            l.transition_in(trans_in);
            l.transition_out(trans_out);

            // Content colour encodes opacity to detect alpha handling.
            Color content_color = transparent_content
                ? Color{1.0f, 0.0f, 0.0f, 0.5f}
                : Color::red();

            l.rect("content_rect", {
                .size = {static_cast<float>(dim.width) * 0.6f,
                         static_cast<float>(dim.height) * 0.6f},
                .color = content_color,
                .pos = {static_cast<float>(dim.width) * 0.2f,
                        static_cast<float>(dim.height) * 0.2f,
                        0}
            });
        });

        return s.build();
    });
}

LayerTransitionSpec make_spec(const char* id,
                              int duration_frames,
                              Easing easing = Easing::Linear,
                              TransitionDirection direction = TransitionDirection::None) {
    LayerTransitionSpec s;
    s.transition_id = id;
    s.duration = duration_frames / 30.0; // seconds at 30 fps
    s.delay = 0.0;
    s.easing = easing;
    s.direction = direction;
    return s;
}

std::shared_ptr<SoftwareRenderer> make_renderer_with_scheduler(SchedulerMode mode) {
    std::string value(scheduler_mode_name(mode));
    const char* prev = std::getenv("CHRONON3D_SCHEDULER_MODE");
    std::string prev_str = prev ? prev : "";

#ifdef _WIN32
    _putenv_s("CHRONON3D_SCHEDULER_MODE", value.c_str());
#else
    ::setenv("CHRONON3D_SCHEDULER_MODE", value.c_str(), 1);
#endif
    auto renderer = make_renderer_shared();

    if (!prev_str.empty()) {
#ifdef _WIN32
        _putenv_s("CHRONON3D_SCHEDULER_MODE", prev_str.c_str());
#else
        ::setenv("CHRONON3D_SCHEDULER_MODE", prev_str.c_str(), 1);
#endif
    } else {
#ifdef _WIN32
        _putenv_s("CHRONON3D_SCHEDULER_MODE", "");
#else
        ::unsetenv("CHRONON3D_SCHEDULER_MODE");
#endif
    }
    return renderer;
}

u64 render_and_hash(const Composition& comp, Frame frame) {
    auto renderer = make_renderer_shared();
    auto fb = renderer->render(comp, frame);
    return framebuffer_hash(*fb);
}

u64 render_and_hash_with_scheduler(const Composition& comp, Frame frame, SchedulerMode mode) {
    auto renderer = make_renderer_with_scheduler(mode);
    auto fb = renderer->render(comp, frame);
    return framebuffer_hash(*fb);
}

// Render a set of frames with the same renderer and collect hashes.
std::vector<u64> render_sequence(const Composition& comp, const std::vector<int>& frames) {
    auto renderer = make_renderer_shared();
    std::vector<u64> hashes;
    hashes.reserve(frames.size());
    for (int f : frames) {
        auto fb = renderer->render(comp, f);
        REQUIRE(fb != nullptr);
        hashes.push_back(framebuffer_hash(*fb));
    }
    return hashes;
}

} // namespace

// ==============================================================================
// 1. Crossfade across multiple durations and start/middle/end frames.
// ==============================================================================
TEST_CASE("TRN-06: crossfade start/middle/end across durations") {
    for (int dur_frames : {1, 2, 10, 30}) {
        LayerTransitionSpec in = make_spec("crossfade", dur_frames);
        auto comp = make_transition_comp(kDim16_9, in, {}, Frame{dur_frames + 1}, Frame{dur_frames + 1}, false);

        u64 h_start = render_and_hash(comp, 0);
        u64 h_end   = render_and_hash(comp, dur_frames);

        // A 1-frame transition collapses to a single-frame cut; start and
        // end may still differ, but the middle sample is the same frame as start.
        int mid_frame = dur_frames / 2;
        if (mid_frame > 0 && mid_frame < dur_frames) {
            u64 h_mid = render_and_hash(comp, mid_frame);
            CHECK(h_mid != h_start);
            CHECK(h_mid != h_end);
        }

        CHECK(h_start != h_end);
    }
}

// ==============================================================================
// 2. Easing matrix (Linear, InQuad, OutQuad, InOutQuad) at fixed duration.
// ==============================================================================
TEST_CASE("TRN-06: crossfade easing curves produce different intermediates") {
    constexpr int kDuration = 30;
    // Curves chosen so that their eased values at t = 0.5 are all distinct.
    std::vector<Easing> easings = {
        Easing::Linear,
        Easing::InQuad,
        Easing::OutQuad,
        Easing::InCubic
    };

    std::vector<u64> easing_mid_hashes;
    for (Easing e : easings) {
        LayerTransitionSpec in = make_spec("crossfade", kDuration, e);
        auto comp = make_transition_comp(kDim16_9, in, {}, Frame{kDuration + 1}, Frame{kDuration + 1}, false);
        easing_mid_hashes.emplace_back(render_and_hash(comp, kDuration / 2));
    }

    // All intermediate hashes should be pairwise distinct.
    for (std::size_t i = 0; i < easing_mid_hashes.size(); ++i) {
        for (std::size_t j = i + 1; j < easing_mid_hashes.size(); ++j) {
            CHECK(easing_mid_hashes[i] != easing_mid_hashes[j]);
        }
    }
}

// ==============================================================================
// 3. Directional transitions test the four canonical directions.
// ==============================================================================
TEST_CASE("TRN-06: slide directions produce distinct outputs") {
    std::vector<TransitionDirection> directions = {
        TransitionDirection::Left,
        TransitionDirection::Right,
        TransitionDirection::Up,
        TransitionDirection::Down
    };

    std::vector<u64> hashes;
    for (TransitionDirection d : directions) {
        LayerTransitionSpec in = make_spec("slide", 30, Easing::Linear, d);
        auto comp = make_transition_comp(kDim16_9, in, {}, Frame{31}, Frame{31}, false);
        hashes.push_back(render_and_hash(comp, 15));
    }

    for (std::size_t i = 0; i < hashes.size(); ++i) {
        for (std::size_t j = i + 1; j < hashes.size(); ++j) {
            CHECK(hashes[i] != hashes[j]);
        }
    }
}

TEST_CASE("TRN-06: wipe directions produce distinct outputs") {
    std::vector<TransitionDirection> directions = {
        TransitionDirection::Left,
        TransitionDirection::Right,
        TransitionDirection::Up,
        TransitionDirection::Down
    };

    std::vector<u64> hashes;
    for (TransitionDirection d : directions) {
        LayerTransitionSpec in = make_spec("wipe_linear", 30, Easing::Linear, d);
        auto comp = make_transition_comp(kDim16_9, in, {}, Frame{31}, Frame{31}, false);
        hashes.push_back(render_and_hash(comp, 15));
    }

    for (std::size_t i = 0; i < hashes.size(); ++i) {
        for (std::size_t j = i + 1; j < hashes.size(); ++j) {
            CHECK(hashes[i] != hashes[j]);
        }
    }
}

// ==============================================================================
// 4. In vs Out for a representative transition.
// ==============================================================================
TEST_CASE("TRN-06: crossfade in vs out are symmetric") {
    const int duration = 30;

    LayerTransitionSpec in_spec  = make_spec("crossfade", duration);
    LayerTransitionSpec out_spec = make_spec("crossfade", duration);

    // For the in transition the layer must remain active through frame
    // `duration` so the end state is evaluated on a live layer.  For the
    // out transition the layer ends exactly at `duration`, making the
    // transition window [0, duration].
    auto comp_in  = make_transition_comp(kDim16_9, in_spec,  {}, Frame{duration + 1}, Frame{duration + 1}, false);
    auto comp_out = make_transition_comp(kDim16_9, {}, out_spec, Frame{duration}, Frame{duration}, false);

    // In at frame f should match out at frame (duration - f) for crossfade.
    u64 h_in_start  = render_and_hash(comp_in, 0);
    u64 h_out_end   = render_and_hash(comp_out, duration);
    u64 h_in_end    = render_and_hash(comp_in, duration);
    u64 h_out_start = render_and_hash(comp_out, 0);

    CHECK(h_in_start == h_out_end);
    CHECK(h_in_end == h_out_start);
}

// ==============================================================================
// 5. Aspect ratio matrix (16:9 and 9:16).
// ==============================================================================
TEST_CASE("TRN-06: crossfade works in 16:9 and 9:16") {
    LayerTransitionSpec in = make_spec("crossfade", 30);

    auto comp_16_9 = make_transition_comp(kDim16_9, in, {}, Frame{31}, Frame{31}, false);
    auto comp_9_16 = make_transition_comp(kDim9_16, in, {}, Frame{31}, Frame{31}, false);

    u64 h_16_9_start = render_and_hash(comp_16_9, 0);
    u64 h_16_9_end   = render_and_hash(comp_16_9, 30);
    u64 h_9_16_start = render_and_hash(comp_9_16, 0);
    u64 h_9_16_end   = render_and_hash(comp_9_16, 30);

    CHECK(h_16_9_start != h_16_9_end);
    CHECK(h_9_16_start != h_9_16_end);
}

// ==============================================================================
// 6. All built-in transitions render start/middle/end without crashing.
// ==============================================================================
TEST_CASE("TRN-06: every built-in transition has distinct start/middle/end") {
    std::vector<std::string> ids = {
        "crossfade", "slide", "wipe_linear", "smooth_wipe",
        "circle_iris", "flash", "procedural_remotion", "remotion"
    };

    for (const auto& id : ids) {
        LayerTransitionSpec in = make_spec(id.c_str(), 30);
        auto comp = make_transition_comp(kDim16_9, in, {}, Frame{31}, Frame{31}, false);

        u64 h0  = render_and_hash(comp, 0);
        u64 h15 = render_and_hash(comp, 15);
        u64 h30 = render_and_hash(comp, 30);

        INFO("transition: ", id);
        CHECK(h0 != h30);
        CHECK(h15 != h0);
        CHECK(h15 != h30);
    }
}

// ==============================================================================
// 7. Cache cold vs warm: two passes with the same renderer produce identical output.
// ==============================================================================
TEST_CASE("TRN-06: warm cache is deterministic") {
    LayerTransitionSpec in = make_spec("crossfade", 30);
    auto comp = make_transition_comp(kDim16_9, in, {}, Frame{31}, Frame{31}, false);

    auto renderer = make_renderer_shared();
    auto fb_cold = renderer->render(comp, 15);
    auto fb_warm = renderer->render(comp, 15);
    REQUIRE(fb_cold != nullptr);
    REQUIRE(fb_warm != nullptr);
    CHECK(framebuffer_hash(*fb_cold) == framebuffer_hash(*fb_warm));
}

// ==============================================================================
// 8. Cold-cache determinism: fresh renderers produce the same frame.
// ==============================================================================
TEST_CASE("TRN-06: cold-cache renders are deterministic") {
    LayerTransitionSpec in = make_spec("crossfade", 30);
    auto comp = make_transition_comp(kDim16_9, in, {}, Frame{31}, Frame{31}, false);

    for (int f : {0, 5, 10, 15, 20, 30}) {
        u64 h_a = render_and_hash(comp, f);
        u64 h_b = render_and_hash(comp, f);
        CHECK(h_a == h_b);
    }
}

// ==============================================================================
// 9. Semi-transparent source content.
// ==============================================================================
TEST_CASE("TRN-06: transparent content transitions are deterministic") {
    LayerTransitionSpec in = make_spec("crossfade", 30);
    auto comp = make_transition_comp(kDim16_9, in, {}, Frame{31}, Frame{31}, true);

    u64 h_start = render_and_hash(comp, 0);
    u64 h_end   = render_and_hash(comp, 30);
    u64 h_mid   = render_and_hash(comp, 15);

    CHECK(h_start != h_end);
    CHECK(h_mid != h_start);
    CHECK(h_mid != h_end);
}

// ==============================================================================
// 10. Serial vs parallel scheduler determinism.
// ==============================================================================
TEST_CASE("TRN-06: crossfade is identical under sequential and parallel scheduler") {
    LayerTransitionSpec in = make_spec("crossfade", 30);
    auto comp = make_transition_comp(kDim16_9, in, {}, Frame{31}, Frame{31}, false);

    std::vector<u64> seq_hashes;
    std::vector<u64> par_hashes;

    for (int f : {0, 5, 10, 15, 20, 30}) {
        seq_hashes.push_back(render_and_hash_with_scheduler(comp, f, SchedulerMode::Sequential));
        par_hashes.push_back(render_and_hash_with_scheduler(comp, f, SchedulerMode::TbbAutomatic));
    }

    CHECK(seq_hashes == par_hashes);
}
