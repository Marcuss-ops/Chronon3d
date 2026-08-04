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
constexpr int kObservedFirstFrame = 30;
constexpr int kObservedLastFrame = 36;
constexpr std::size_t kObservedFrameCount =
    static_cast<std::size_t>(kObservedLastFrame - kObservedFirstFrame + 1);

using FrameHashes = std::array<u64, kObservedFrameCount>;

std::shared_ptr<SoftwareRenderer> make_reproduction_renderer() {
    auto renderer = test::make_renderer_shared();
    RenderSettings settings = renderer->render_settings();
    settings.motion_blur.mode = MotionBlurMode::Off;
    renderer->set_settings(settings);
    return renderer;
}

FrameHashes render_independent_frames(const Composition& composition) {
    FrameHashes hashes{};
    for (int frame = kObservedFirstFrame; frame <= kObservedLastFrame; ++frame) {
        auto renderer = make_reproduction_renderer();
        auto framebuffer = renderer->render(composition, Frame{frame});
        REQUIRE_MESSAGE(framebuffer != nullptr,
                        "independent render returned no framebuffer at frame " << frame);
        hashes[static_cast<std::size_t>(frame - kObservedFirstFrame)] =
            test::framebuffer_hash(*framebuffer);
        REQUIRE_MESSAGE(hashes[static_cast<std::size_t>(frame - kObservedFirstFrame)] != 0,
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

    for (int frame = kObservedFirstFrame; frame <= kObservedLastFrame; ++frame) {
        auto framebuffer = renderer->render(composition, Frame{frame});
        REQUIRE_MESSAGE(framebuffer != nullptr,
                        "sequential render returned no framebuffer at frame " << frame);
        hashes[static_cast<std::size_t>(frame - kObservedFirstFrame)] =
            test::framebuffer_hash(*framebuffer);
        REQUIRE_MESSAGE(hashes[static_cast<std::size_t>(frame - kObservedFirstFrame)] != 0,
                        "sequential render produced an empty hash at frame " << frame);
    }
    return hashes;
}

} // namespace

TEST_CASE("LightTransition cache reproduction: independent and sequential frames match") {
    const auto composition = content::launches::light_transition_copper_flash();

    const auto independent = render_independent_frames(composition);
    const auto sequential = render_sequential_frames(composition);

    for (int frame = kObservedFirstFrame; frame <= kObservedLastFrame; ++frame) {
        const auto index = static_cast<std::size_t>(frame - kObservedFirstFrame);
        INFO("LightTransitionCopperFlash divergence frame=" << frame
             << " independent_hash=" << independent[index]
             << " sequential_hash=" << sequential[index]);
        CHECK_MESSAGE(independent[index] == sequential[index],
                      "fresh-runtime and sequential render diverged at frame " << frame);
    }
}
