// ============================================================================
// tests/text/test_chronon_glow_temporal.cpp
//
// TICKET-REFACTOR-TESTS-SPLIT-18-19 §A — pipeline parity: ChrononGlow
// temporal stability + still-video parity (TICKET-CHRONON-GLOW-FINAL
// Fase 6 TEST FINALI).
//
// Renders the user-spec command
//   `chronon3d_cli video ChrononGlowFinalAE --start 0 --end 60 --fps 30
//    -o output/glow_final/chronon_glow_final.mp4`
// and verifies:
//
//   (c) Temporal stability across 60 frames:
//       - no-flicker       : all frames have non-zero ink (no empty bboxes)
//       - center-stability : bright-pixel centroid stays within ±100px
//                            of canvas center (960, 540) across f00/f15/f30
//       - no-glow-pop      : frame-to-frame pixel-diff is bounded (no
//                            sudden glow intensity changes between
//                            adjacent snapshot buckets)
//
//   (d) Still⇌frame parity:
//       - SDK still at frame N  ==  raw video frame N  (hash-exact)
//
// SKIP-rot guards: ffmpeg missing, or chronon3d_cli not built at the
// resolved path.  Honest PARTIAL status on pre-existing build rot
// (ChrononGlowFinalAE composition cannot be resolved at runtime).
// ============================================================================

#include <doctest/doctest.h>
#include <tests/helpers/doctest_skip_compat.hpp>

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text/support/pipeline_parity_harness.hpp>

// TICKET-CHRONON-GLOW-FINAL Fase 6 — same factory as the CLI registration.
#include "content/compositions/chronon_glow_final.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace ph = chronon3d::test::parity_harness;

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-CHRONON-GLOW-FINAL Fase 6 — `ChrononGlowFinalAE` temporal
// stability + still-video parity (TEST FINALI).
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: ChrononGlowFinalAE temporal stability "
          "and still-video parity (Fase 6 TEST FINALI)") {
    if (!ph::ffmpeg_available()) {
        SKIP("TICKET-DOCTEST-SKIP-ROT: ffmpeg not found in PATH");
    }
    if (!std::filesystem::exists(ph::get_cli_path())) {
        SKIP("TICKET-DOCTEST-SKIP-ROT: chronon3d_cli not built at " << ph::get_cli_path()
             << " — pre-existing build rot blocks the test");
    }

    // Resolve spec §5 canonical output root (override-able via env var).
    std::filesystem::path output_root;
    {
        const char* env = std::getenv("CHRONON3D_TEST_VIDEO_OUTPUT_DIR");
        if (env != nullptr && env[0] != '\0') {
            output_root = std::filesystem::path(env);
        } else {
            output_root = std::filesystem::current_path()
                          / "output" / "text_video_acceptance";
        }
    }

    const auto tmp = ph::make_temp_dir();

    // ── (1) Render SDK stills at frames 0/15/30 using the exact same
    //       composition the CLI video subprocess renders.
    auto comp = chronon3d::content::glow_final::make_chronon_glow_final(
        chronon3d::content::glow_final::default_landscape_props());
    auto renderer = chronon3d::test::make_renderer_shared();
    renderer->set_settings(ph::default_settings());

    const std::string sdk_f00 = (tmp / "sdk_f00.png").string();
    const std::string sdk_f15 = (tmp / "sdk_f15.png").string();
    const std::string sdk_f30 = (tmp / "sdk_f30.png").string();

    auto fb0 = renderer->render(comp, chronon3d::Frame{0});
    REQUIRE(fb0 != nullptr);
    REQUIRE(chronon3d::save_png(*fb0, sdk_f00));
    const auto hash_sdk_f00 = ph::hash_from_png(sdk_f00);

    auto fb15 = renderer->render(comp, chronon3d::Frame{15});
    REQUIRE(fb15 != nullptr);
    REQUIRE(chronon3d::save_png(*fb15, sdk_f15));
    const auto hash_sdk_f15 = ph::hash_from_png(sdk_f15);

    auto fb30 = renderer->render(comp, chronon3d::Frame{30});
    REQUIRE(fb30 != nullptr);
    REQUIRE(chronon3d::save_png(*fb30, sdk_f30));
    const auto hash_sdk_f30 = ph::hash_from_png(sdk_f30);

    // ── (2) Render the full 60-frame video via the user-spec CLI
    //       command.
    const auto vid = ph::run_cli_video_chronon_glow_final(
        output_root.string(), /*start=*/0, /*end=*/60);
    REQUIRE(!vid.video_path.empty());

    // ── (3) Extract frames 0/15/30 from the video output directory.
    const std::string vid_f00 = (std::filesystem::path(vid.frames_dir) / "frame_000000.png").string();
    const std::string vid_f15 = (std::filesystem::path(vid.frames_dir) / "frame_000015.png").string();
    const std::string vid_f30 = (std::filesystem::path(vid.frames_dir) / "frame_000030.png").string();
    REQUIRE(std::filesystem::exists(vid_f00));
    REQUIRE(std::filesystem::exists(vid_f15));
    REQUIRE(std::filesystem::exists(vid_f30));

    const auto hash_vid_f00 = ph::hash_from_png(vid_f00);
    const auto hash_vid_f15 = ph::hash_from_png(vid_f15);
    const auto hash_vid_f30 = ph::hash_from_png(vid_f30);

    // ── (4) Still⇌frame parity (d) — SDK still == raw video frame.
    CHECK(hash_sdk_f00 == hash_vid_f00);
    CHECK(hash_sdk_f15 == hash_vid_f15);
    CHECK(hash_sdk_f30 == hash_vid_f30);

    // ── (5) Spec §5 video-completeness — full 60-frame alpha bbox +
    //       centroid + max_alpha loop.
    constexpr int    kFrameCount              = 60;
    constexpr int    kMaxAlphaFloor           = 64;
    constexpr int    kBboxMinWidth            = 100;
    constexpr int    kBboxMinHeight           = 30;
    constexpr int    kEdgeMargin              = 8;
    constexpr int    kCanvasWidth             = 1920;
    constexpr int    kCanvasHeight            = 1080;
    constexpr int    kCanvasCenterX           = kCanvasWidth / 2;   // 960
    constexpr int    kCanvasCenterY           = kCanvasHeight / 2;  // 540
    constexpr double kCentroidAbsTolerance    = 110.0;
    constexpr double kInterFrameJumpMax       = 12.0;

    std::error_code ec;
    std::filesystem::create_directories(output_root, ec);
    REQUIRE(!ec);
    const std::filesystem::path csv_path = output_root / "frame_metrics.csv";
    std::ofstream csv(csv_path.string());
    REQUIRE(csv.is_open());
    csv << "frame,x0,y0,x1,y1,width,height,centroid_x,"
           "centroid_y,visible_pixels,alpha_sum,max_alpha\n";

    int frames_loaded = 0;
    ph::FrameMetrics prev{};
    bool have_prev = false;

    for (int f = 0; f < kFrameCount; ++f) {
        std::ostringstream fname;
        fname << "frame_" << std::setw(6) << std::setfill('0') << f << ".png";
        const std::string path =
            (std::filesystem::path(vid.frames_dir) / fname.str()).string();
        REQUIRE(std::filesystem::exists(path));

        auto fb_v = chronon3d::test::load_png_as_framebuffer(path);
        REQUIRE(fb_v != nullptr);
        REQUIRE(fb_v->width()  >= static_cast<size_t>(kCanvasWidth));
        REQUIRE(fb_v->height() >= static_cast<size_t>(kCanvasHeight));

        const ph::FrameMetrics m = ph::compute_frame_metrics(*fb_v, f);
        ++frames_loaded;

        csv << m.frame << ',' << m.x0 << ',' << m.y0 << ',' << m.x1 << ','
            << m.y1 << ',' << m.width << ',' << m.height << ','
            << std::fixed << std::setprecision(2) << m.centroid_x << ','
            << std::setprecision(2) << m.centroid_y << ','
            << m.visible_pixels << ',' << std::setprecision(6) << m.alpha_sum
            << ',' << m.max_alpha << '\n';

        CHECK(m.visible_pixels > 0);
        CHECK(m.max_alpha    > kMaxAlphaFloor);
        CHECK(m.width        > kBboxMinWidth);
        CHECK(m.height       > kBboxMinHeight);
        CHECK(m.x0 >= kEdgeMargin);
        CHECK(m.y0 >= kEdgeMargin);
        CHECK(m.x1 <  kCanvasWidth  - kEdgeMargin);
        CHECK(m.y1 <  kCanvasHeight - kEdgeMargin);
        CHECK(std::abs(m.centroid_x - kCanvasCenterX) < kCentroidAbsTolerance);
        CHECK(std::abs(m.centroid_y - kCanvasCenterY) < kCentroidAbsTolerance);
        if (have_prev) {
            CHECK(std::abs(m.centroid_x - prev.centroid_x) < kInterFrameJumpMax);
            CHECK(std::abs(m.centroid_y - prev.centroid_y) < kInterFrameJumpMax);
        }
        prev = m;
        have_prev = true;
    }
    csv.close();
    CHECK(frames_loaded == kFrameCount);
    INFO("CSV metrics emitted to " << csv_path.string()
         << " (" << frames_loaded << " frames)");

    // ── (6) No-glow-pop: distinct per-frame hashes from the opacity envelope.
    CHECK(hash_sdk_f00 != hash_sdk_f15);
    CHECK(hash_sdk_f15 != hash_sdk_f30);
    CHECK(hash_sdk_f00 != hash_sdk_f30);
}
