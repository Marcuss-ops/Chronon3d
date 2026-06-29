// ═══════════════════════════════════════════════════════════════════════════
//  cinematic_showcase_fixture.cpp — Definition site for
//  chronon3d::testing::cinematic::render_frames().
//
//  Phase-2.2 mechanical split — keeps the inner REQUIRE assertions
//  owned by a single TU rather than duplicated or inline-emitted in
//  every split test TU.  Linked into the single
//  chronon3d_cinematic_camera_showcase_tests executable alongside
//  cinematic/test_cinematic_{smoke,presets,determinism,artifacts}.cpp.
// ═══════════════════════════════════════════════════════════════════════════

#include "cinematic_showcase_fixture.hpp"

namespace chronon3d::testing::cinematic {

FrameCache render_frames(SoftwareRenderer& renderer, const Composition& comp) {
    FrameCache out;
    for (int f : runtime_kf()) {
        const auto t0 = std::chrono::steady_clock::now();
        auto fb = renderer.render_frame(comp, Frame{f});
        const auto t1 = std::chrono::steady_clock::now();
        REQUIRE(fb != nullptr);
        REQUIRE(fb->width()  == kCompW);
        REQUIRE(fb->height() == kCompH);
        const float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
        FrameMetrics m = compute_metrics(*fb, ms);
        out.emplace(f, std::make_pair(std::move(m), std::move(fb)));
    }
    return out;
}

} // namespace chronon3d::testing::cinematic
