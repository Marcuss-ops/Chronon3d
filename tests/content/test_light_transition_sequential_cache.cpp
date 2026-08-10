// =============================================================================
// test_light_transition_sequential_cache.cpp
//
// Reproduction for the LightTransition sequential graph-refresh divergence:
// fresh-runtime renders of the critical transition frames are compared with
// the same frames rendered by one renderer after a sequential 0..29 warmup.
// This is intentionally a reproduction-only test. It does not change cache
// policy or production refresh behavior.
// =============================================================================

#include <doctest/doctest.h>

#include <content/launches/light_transition_sound_smoke.hpp>
#include <tests/helpers/test_utils.hpp>

#include <array>
#include <memory>

using namespace chronon3d;

namespace {

constexpr int kWarmupFirstFrame = 0;
constexpr int kWarmupLastFrame = 29;
// Sparse checkpoints cover the warmup boundary and the end of the
// transition while keeping this regression test bounded in memory: a fresh
// renderer is intentionally created for each independent checkpoint.
constexpr std::array<int, 3> kObservedFrames{20, 29, 36};
constexpr std::size_t kObservedFrameCount = kObservedFrames.size();

using FrameHashes = std::array<u64, kObservedFrameCount>;

std::shared_ptr<SoftwareRenderer> make_reproduction_renderer() {
    auto renderer = test::make_renderer_shared();
    RenderSettings settings = renderer->render_settings();
    settings.motion_blur.mode = MotionBlurMode::Off;
    // The regression assertion compares framebuffer hashes; verbose graph
    // diagnostics add substantial allocation/log pressure while providing no
    // extra signal for this test.
    settings.diagnostics.enabled = false;
    renderer->set_settings(settings);
    return renderer;
}

FrameHashes render_independent_frames(const Composition& composition) {
    FrameHashes hashes{};
    for (std::size_t index = 0; index < kObservedFrames.size(); ++index) {
        const int frame = kObservedFrames[index];
        auto renderer = make_reproduction_renderer();
        auto framebuffer = renderer->render(composition, Frame{frame});
        REQUIRE_MESSAGE(framebuffer != nullptr,
                        "independent render returned no framebuffer at frame " << frame);
        hashes[index] = test::framebuffer_hash(*framebuffer);
        REQUIRE_MESSAGE(hashes[index] != 0,
                        "independent render produced an empty hash at frame " << frame);
    }
    return hashes;
}

FrameHashes render_sequential_frames(const Composition& composition) {
    FrameHashes hashes{};
    auto renderer = make_reproduction_renderer();

    // Preserve the exact sequential path: the same renderer/runtime is used
    // for every frame, including the transition boundary warmup.
    for (int frame = kWarmupFirstFrame; frame <= kWarmupLastFrame; ++frame) {
        auto framebuffer = renderer->render(composition, Frame{frame});
        REQUIRE_MESSAGE(framebuffer != nullptr,
                        "sequential warmup returned no framebuffer at frame " << frame);
    }

    for (std::size_t index = 0; index < kObservedFrames.size(); ++index) {
        const int frame = kObservedFrames[index];
        auto framebuffer = renderer->render(composition, Frame{frame});
        REQUIRE_MESSAGE(framebuffer != nullptr,
                        "sequential render returned no framebuffer at frame " << frame);
        hashes[index] = test::framebuffer_hash(*framebuffer);
        REQUIRE_MESSAGE(hashes[index] != 0,
                        "sequential render produced an empty hash at frame " << frame);
    }
    return hashes;
}

} // namespace

TEST_CASE("LightTransition cache reproduction: independent and sequential frames match") {
    const auto composition = content::launches::light_transition_copper_flash();


    const auto independent = render_independent_frames(composition);
    const auto sequential = render_sequential_frames(composition);

    int first_divergent_frame = -1;
    for (std::size_t index = 0; index < kObservedFrames.size(); ++index) {
        const int frame = kObservedFrames[index];
        const bool matches = independent[index] == sequential[index];
        if (!matches && first_divergent_frame < 0) {
            first_divergent_frame = frame;
        }
        INFO("LightTransitionCopperFlash frame=" << frame
             << " independent_hash=" << independent[index]
             << " sequential_hash=" << sequential[index]
             << " first_divergent_frame=" << first_divergent_frame);
        CHECK_MESSAGE(matches,
                      "fresh-runtime and sequential render diverged at frame " << frame);
    }
    INFO("LightTransitionCopperFlash first_divergent_frame=" << first_divergent_frame);
}
