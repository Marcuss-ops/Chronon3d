// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/glow_ab/glow_temporal_tests.cpp
//
// TICKET-GLOW-CERTIFICATION — Azione 2: temporal glow acceptance tests.
//
//   1. All 60 frames: no empty frame, no glow pop (inter-frame luma
//      delta < 25.0), pulse peak at frame 15.
//   2. Pulse timing: frame 0→15→30 envelope — f15 halo > f0 halo,
//      f15 halo > f30 halo, centroid stable within 3 px.
//
// Uses the canonical ChrononGlowFinalAE factory (content/compositions/chronon_glow_final.hpp)
// plus existing test helpers (alpha_bbox, alpha_centroid, average_luma_rect,
// framebuffer_hash) from tests/helpers and tests/text_golden/text_clip/.
//
// MP4 decode + SSIM check (user spec §12) is deferred to
// tools/check_glow_temporal.py — requires ffmpeg + a working build host
// with chronon3d_cli binary.  Documented as §honest-limitation per
// AGENTS.md §13 pattern.
//
// AGENTS.md Cat-2 freeze-compliant: zero new public SDK API.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>
#include "content/compositions/chronon_glow_final.hpp"

#include <cmath>
#include <memory>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::test::glow_final::ChrononGlowProps;

namespace {

/// Render the canonical ChrononGlowFinalAE composition at the given frame.
std::shared_ptr<Framebuffer> render_at(
    std::shared_ptr<SoftwareRenderer> renderer,
    ChrononGlowProps props,
    int frame_idx) {
    return renderer->render(
        chronon3d::test::glow_final::make_chronon_glow_final_for_test(
            props, renderer->font_engine()),
        Frame{frame_idx});
}

/// Average luma over a halfW×halfW square centred on (cx, cy).
float sample_luma(const Framebuffer& fb, float cx, float cy, int halfW) {
    int x0 = std::max(0, static_cast<int>(cx) - halfW);
    int y0 = std::max(0, static_cast<int>(cy) - halfW);
    int x1 = std::min(fb.width(),  static_cast<int>(cx) + halfW + 1);
    int y1 = std::min(fb.height(), static_cast<int>(cy) + halfW + 1);
    return average_luma_rect(fb, x0, y0, x1, y1);
}

/// Simple L2 distance between two Vec2.
float dist(Vec2 a, Vec2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

/// Struct holding per-frame metrics used by the temporal sweep.
struct FrameMetrics {
    int     frame{0};
    AlphaBBox bbox;
    Vec2    centroid{-1.0f, -1.0f};
    float   core_luma{0.0f};
    float   halo_luma{0.0f};   // luma at centroid + 60 px offset
};

/// Render `count` frames from the canonical composition and collect metrics.
/// Halo offset is passed explicitly so the caller can choose 16:9 or 9:16.
std::vector<FrameMetrics> collect_frame_metrics(
    std::shared_ptr<SoftwareRenderer> renderer,
    ChrononGlowProps props,
    int count,
    int halo_offset_x) {
    std::vector<FrameMetrics> metrics;
    metrics.reserve(static_cast<size_t>(count));

    constexpr int kHalfW = 4;  // 9×9 sample window
    for (int f = 0; f < count; ++f) {
        auto fb = render_at(renderer, props, f);
        REQUIRE(fb != nullptr);

        FrameMetrics m;
        m.frame      = f;
        m.bbox       = alpha_bbox(*fb, 0.01f);
        m.centroid   = alpha_centroid(*fb);
        m.core_luma  = sample_luma(*fb, m.centroid.x, m.centroid.y, kHalfW);
        m.halo_luma  = sample_luma(*fb,
                         m.centroid.x + static_cast<float>(halo_offset_x),
                         m.centroid.y, kHalfW);
        metrics.push_back(m);
    }
    return metrics;
}

}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST 1 — All 60 frames: no empty, no glow pop, pulse peak at frame 15
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow temporal: 60-frame sweep — no empty frame, no glow pop, pulse peak at f15") {
    auto renderer = make_renderer_shared();

    ChrononGlowProps props = chronon3d::test::glow_final::default_landscape_props();
    props.glow_enabled = true;

    constexpr int kFrameCount    = 60;
    constexpr int kHaloOffset16x9 = 60;   // landscape halo offset
    constexpr float kMaxLumaJump  = 25.0f; // max allowed inter-frame luma delta

    auto metrics = collect_frame_metrics(renderer, props, kFrameCount, kHaloOffset16x9);
    REQUIRE(metrics.size() == static_cast<size_t>(kFrameCount));

    // ── No empty frame ──────────────────────────────────────────────────
    for (const auto& m : metrics) {
        INFO("Frame ", m.frame, ": bbox empty");
        CHECK_FALSE(m.bbox.empty());
    }

    // ── No glow pop: inter-frame luma delta bounded ────────────────────
    for (size_t i = 1; i < metrics.size(); ++i) {
        float delta = std::abs(metrics[i].core_luma - metrics[i - 1].core_luma);
        INFO("Frame ", metrics[i].frame, ": luma jump = ", delta,
             " (prev = ", metrics[i - 1].core_luma,
             ", curr = ", metrics[i].core_luma, ")");
        CHECK(delta < kMaxLumaJump);
    }

    // ── Pulse peak at frame 15 ─────────────────────────────────────────
    // The canonical ChrononGlowFinalAE composition uses
    // opacity_for_frame(): f0=0.40, f15=0.85, f30=0.50.
    // So core luminance should peak at or near frame 15.
    const auto& f0  = metrics[0];
    const auto& f15 = metrics[15];
    const auto& f30 = metrics[30];

    INFO("f0  core_luma = ", f0.core_luma,   "  halo_luma = ", f0.halo_luma);
    INFO("f15 core_luma = ", f15.core_luma,  "  halo_luma = ", f15.halo_luma);
    INFO("f30 core_luma = ", f30.core_luma,  "  halo_luma = ", f30.halo_luma);

    // Pulse rises from 0 → 15
    CHECK(f15.halo_luma > f0.halo_luma);
    // Pulse falls from 15 → 30
    CHECK(f15.halo_luma > f30.halo_luma);
    // Centroid stays stable across the pulse
    CHECK(dist(f0.centroid, f15.centroid) < 3.0f);
    CHECK(dist(f15.centroid, f30.centroid) < 3.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 2 — Pulse timing: frame 0→15→30 envelope (explicit contracted check)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow temporal: pulse reaches expected peak at frame 15 (contracted)") {
    auto renderer = make_renderer_shared();

    ChrononGlowProps props = chronon3d::test::glow_final::default_landscape_props();
    props.glow_enabled = true;

    constexpr int kHaloOffset16x9 = 60;
    constexpr int kHalfW = 4;

    auto render_and_measure = [&](int frame_idx) -> FrameMetrics {
        auto fb = render_at(renderer, props, frame_idx);
        REQUIRE(fb != nullptr);
        FrameMetrics m;
        m.frame    = frame_idx;
        m.bbox     = alpha_bbox(*fb, 0.01f);
        m.centroid = alpha_centroid(*fb);
        m.core_luma = sample_luma(*fb, m.centroid.x, m.centroid.y, kHalfW);
        m.halo_luma = sample_luma(*fb,
                         m.centroid.x + static_cast<float>(kHaloOffset16x9),
                         m.centroid.y, kHalfW);
        return m;
    };

    auto f0  = render_and_measure(0);
    auto f15 = render_and_measure(15);
    auto f30 = render_and_measure(30);

    INFO("f0  halo_luma = ", f0.halo_luma,   "  centroid = (", f0.centroid.x,  ", ", f0.centroid.y,  ")");
    INFO("f15 halo_luma = ", f15.halo_luma,  "  centroid = (", f15.centroid.x, ", ", f15.centroid.y, ")");
    INFO("f30 halo_luma = ", f30.halo_luma,  "  centroid = (", f30.centroid.x, ", ", f30.centroid.y, ")");

    // ── Contract: f15 is the peak ──────────────────────────────────────
    CHECK(f15.halo_luma > f0.halo_luma);
    CHECK(f15.halo_luma > f30.halo_luma);

    // ── Contract: centroid stays within 3 px across all 3 frames ──────
    CHECK(dist(f0.centroid, f15.centroid) < 3.0f);
    CHECK(dist(f15.centroid, f30.centroid) < 3.0f);

    // ── Contract: no empty frame ───────────────────────────────────────
    CHECK_FALSE(f0.bbox.empty());
    CHECK_FALSE(f15.bbox.empty());
    CHECK_FALSE(f30.bbox.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 3 — MP4 decode + SSIM stub (§honest-limitation forward-point)
// ═══════════════════════════════════════════════════════════════════════════
//
// The user spec (§12) requires:
//   - Decode glow.mp4 with ffmpeg → 60 PNG frames
//   - Compare raw PNGs vs decoded PNGs via ffmpeg SSIM filter
//   - Assert SSIM mean ≥ 0.97, no frame with completely lost halo,
//     no severe banding
//
// This can only be executed on a working build host with:
//   1. chronon3d_cli binary (to render the MP4)
//   2. ffmpeg with libx264 + SSIM filter support
//
// The Python script tools/check_glow_temporal.py contains the
// canonical SSIM check when the prerequisites are met.
//
// Per AGENTS.md §honesty "non segnare verde una suite che restituisce
// failure", this TEST_CASE documents the deferred contract and
// emits a WARN-level skip with the canonical rebuild hint.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow temporal: MP4 decode SSIM (forward-point, deferred to working build host)") {
    // This test cannot run on the current VPS because:
    //   (a) chronon3d_cli binary is not linkable (vcpkg glm/magic_enum absent)
    //   (b) ffmpeg may not be installed or may lack SSIM filter
    //
    // When prerequisites are met, invoke:
    //   python3 tools/check_glow_temporal.py ssim \
    //     output/glow_acceptance/glow.mp4
    //
    // Expected: SSIM mean ≥ 0.97, 60 decoded frames, no empty decoded frame.

    MESSAGE("MP4 SSIM check deferred to working build host — "
            "see tools/check_glow_temporal.py §ssim contract");

    // Forward-point ticket: TICKET-GLOW-CERTIFICATION-MP4-SSIM
    // Opened in docs/FOLLOWUP_TICKETS.md §Open Blockers after this commit.
}
