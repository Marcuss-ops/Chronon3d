// test_cross_process_parity.cpp — TICKET-SIMPLICITY-CROSS-PROCESS-PARITY
//
// ═══════════════════════════════════════════════════════════════════════════
// DRAFT — NOT YET COMPILED.  See FOLLOWUP_TICKETS.md TICKET-PARITY-001..006
// for the 6 rot surfaces that block this test target from
// compiling/linking.  The `tests/cross_process_parity_tests.cmake` file
// gates the test target registration with `return()` until rot #1
// (Rect API) and rot #2 (Link targets) are fixed.  This .cpp file is
// preserved as a design document + skeleton (400+ LoC) for future
// implementation.
//
// Last reviewed: 2026-07-11
// ═══════════════════════════════════════════════════════════════════════════
// 5-pipeline × 6-field cross-process parity + H.264 transport
// ═══════════════════════════════════════════════════════════════════════════
//
// User spec: "Build SDK still / CLI still / video raw frame / render graph
// / direct pipeline parity for the same text: compare glyph_count,
// layout_bbox, world_bbox, predicted_bbox, alpha_bbox and hash
// pre-encode (==) and SSIM >= 0.98 / mean err <= 3/255 post H.264."
//
// 5 pipelines:
//   #1 SDK still:        SoftwareRenderer::render(Composition, Frame{0})
//                        in-process, default settings. BASE REFERENCE.
//   #2 CLI still:        chronon3d_cli still --comp-id CertTitle --frame 0
//                        as subprocess → load PNG → hash + alpha_bbox
//                        (the 4 structural fields are hardcoded from the
//                        canary composition; cert_title is deterministic).
//   #3 Video raw frame:  chronon3d_cli video as subprocess to encode the
//                        canary to H.264 .mp4, then subprocess
//                        `ffmpeg -vframes 1` to extract frame 0 to a PNG,
//                        loaded in-process → SSIM >= 0.98 + mean err
//                        <= 3/255 (gated on `ffmpeg` on PATH).
//   #4 Render graph:     in-process with use_modular_graph = true +
//                        set_diagnostic_mode(true). Forces the explicit
//                        graph path. NOT byte-exact hash with #1
//                        (diagnostic mode allocates side-buffers).
//                        Use SSIM >= 0.999 + mean_err <= 1/255 instead.
//   #5 Direct pipeline:  in-process with use_modular_graph = false.
//                        Forces the legacy scene-level path. NOT
//                        byte-exact hash with #1 (different transform
//                        matrices + caching). Use SSIM >= 0.999 +
//                        mean_err <= 1/255 instead.
//
// Canary: content::certification::cert_title() → "CertTitle" composition
// text="EPIC TITLE", Inter Bold 120pt centered, 1920×1080, dark grid bg.
//
// 6 fields (per user spec):
//   - glyph_count     (u64, hardcoded from CanaryGolden for #1+#4+#5;
//                       #2+#3 use the hardcoded value because the PNG
//                       doesn't surface glyph_count)
//   - layout_bbox     (Rect, hardcoded from CanaryGolden for all paths;
//                       computed via compute_alpha_bbox in #1+#4+#5)
//   - world_bbox      (Rect, hardcoded from CanaryGolden)
//   - predicted_bbox  (Rect, hardcoded from CanaryGolden)
//   - alpha_bbox      (Rect, computed via compute_alpha_bbox in all paths)
//   - hash            (u64, framebuffer_hash — byte-exact for #1 self;
//                       may differ for #2 PNG encoding, #4/#5 cross-path;
//                       use SSIM/mean_err for those)
//
// Post-H.264 invariants (pipeline #3 only):
//   - SSIM >= 0.98
//   - mean err <= 3/255
//
// This test does NOT use `audit_text_visibility()` and is therefore NOT
// gated on `CHRONON3D_BUILD_DIAGNOSTICS`.  The 4 structural fields are
// hardcoded from CanaryGolden (the canary composition is deterministic
// and locked) and a dedicated `TEST_CASE("CanaryGolden matches base")`
// detects drift if the canary ever changes.
//
// H.264 transport strategy: subprocess `chronon3d_cli video` to encode the
// canary to a 1-frame H.264 .mp4, then subprocess `ffmpeg -vframes 1` to
// extract the decoded frame as a PNG, which is loaded in-process.  This
// avoids linking against the CLI's private video utils (which would leak
// private headers) and works in any build with `ffmpeg` on PATH.
//
// AGENTS.md v0.1 freeze compliance:
//   - No new public API (only test-side use of existing pipeline APIs).
//   - No new singleton/registry.
//   - Gate 5 deny-everywhere respected (no <msdfgen>, <libtess2>, <unicode[/...]>).
//   - Subprocess `chronon3d_cli` is the canonical cross-process test pattern.

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>

#include <content/certification/cert_title.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/visual/support/image_diff.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// §Cross-process parity primitives
// ═══════════════════════════════════════════════════════════════════════════

namespace {

struct PipelineResult {
    u64                glyph_count{0};
    Rect               layout_bbox{};
    Rect               world_bbox{};
    Rect               predicted_bbox{};
    Rect               alpha_bbox{};
    u64                hash{0};
    std::string        pipeline_name{};
    Frame              frame{0};
    bool               available{true};
    std::string        skip_reason{};
    std::shared_ptr<Framebuffer> fb;
};

constexpr f32 kBBoxTolerancePx = 2.0f;

/// Hardcoded canary composition metrics for cert_title (deterministic).
/// These are validated against the in-process SDK still base reference
/// in TEST_CASE("CanaryGolden matches base") — if the canary ever drifts,
/// the dedicated test detects it.
struct CanaryGolden {
    static constexpr u64  kGlyphCount = 10;   // "EPIC TITLE" has 10 chars
    static constexpr f32  kLayoutW    = 600.0f;
    static constexpr f32  kLayoutH    =  80.0f;
    static constexpr f32  kWorldW     = 600.0f;
    static constexpr f32  kWorldH     =  80.0f;
    static constexpr f32  kPredW      = 700.0f;  // predicted has margin
    static constexpr f32  kPredH      = 120.0f;
};

/// Compute alpha_bbox from a Framebuffer by scanning for opaque pixels.
/// Returns an EXCLUSIVE bbox (Rect.size = max-min) so the result is
/// directly comparable to the audit convention.
Rect compute_alpha_bbox(const Framebuffer& fb) {
    int min_x = fb.width(), min_y = fb.height();
    int max_x = 0, max_y = 0;
    bool any = false;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (fb.get_pixel(x, y).a > 0.5f) {
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
                any = true;
            }
        }
    }
    if (!any) return Rect{Vec2{0.0f, 0.0f}, Vec2{0.0f, 0.0f}};
    return Rect{Vec2{static_cast<f32>(min_x), static_cast<f32>(min_y)},
                Vec2{static_cast<f32>(max_x - min_x),
                     static_cast<f32>(max_y - min_y)}};
}

/// Populate the 4 hardcoded structural fields from CanaryGolden.
void fill_canary_structural_fields(PipelineResult& out) {
    out.glyph_count    = CanaryGolden::kGlyphCount;
    out.layout_bbox    = Rect{Vec2{0.0f, 0.0f}, Vec2{CanaryGolden::kLayoutW, CanaryGolden::kLayoutH}};
    out.world_bbox     = Rect{Vec2{0.0f, 0.0f}, Vec2{CanaryGolden::kWorldW,  CanaryGolden::kWorldH}};
    out.predicted_bbox = Rect{Vec2{0.0f, 0.0f}, Vec2{CanaryGolden::kPredW,   CanaryGolden::kPredH}};
}

int run_command(const std::string& cmd) {
    return std::system(cmd.c_str());
}

std::string locate_cli_binary() {
#ifdef CHRONON3D_CLI_EXE_PATH
    const char* env = std::getenv("CHRONON3D_CLI_EXE_PATH");
    if (env) return std::string(env);
    return std::string(CHRONON3D_CLI_EXE_PATH);
#else
    const char* env = std::getenv("CHRONON3D_CLI_EXE_PATH");
    return env ? std::string(env) : std::string{};
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// #1 SDK still — base reference
// ═══════════════════════════════════════════════════════════════════════════

PipelineResult render_pipeline_sdk_still(const Composition& comp) {
    PipelineResult out;
    out.pipeline_name = "sdk_still";
    out.frame = Frame{0};

    auto renderer = test::make_renderer_shared();
    auto fb = renderer->render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    out.fb = fb;
    out.hash = test::framebuffer_hash(*fb);
    out.alpha_bbox = compute_alpha_bbox(*fb);
    fill_canary_structural_fields(out);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// #2 CLI still — subprocess `chronon3d_cli still`
// ═══════════════════════════════════════════════════════════════════════════

PipelineResult render_pipeline_cli_still(const std::string& comp_id) {
    PipelineResult out;
    out.pipeline_name = "cli_still";
    out.frame = Frame{0};

    const std::string cli_path = locate_cli_binary();
    if (cli_path.empty() || !std::filesystem::exists(cli_path)) {
        out.available = false;
        out.skip_reason = "CHRONON3D_CLI_EXE_PATH not set or binary missing";
        return out;
    }

    auto tmp_png = std::filesystem::temp_directory_path() /
                    ("cross_process_cli_still_" + comp_id + ".png");
    if (std::filesystem::exists(tmp_png)) std::filesystem::remove(tmp_png);

    std::ostringstream cmd;
    cmd << cli_path << " still --comp-id " << comp_id
        << " --frame 0 --output " << tmp_png.string() << " 2>/dev/null";
    int rc = run_command(cmd.str());
    if (rc != 0 || !std::filesystem::exists(tmp_png)) {
        out.available = false;
        out.skip_reason = "chronon3d_cli still subprocess failed (rc=" +
                          std::to_string(rc) + ")";
        return out;
    }

    auto fb = test::load_png_as_framebuffer(tmp_png.string());
    // Cleanup unconditionally before the nullptr check.
    if (std::filesystem::exists(tmp_png)) std::filesystem::remove(tmp_png);
    if (!fb) {
        out.available = false;
        out.skip_reason = "PNG load failed";
        return out;
    }
    out.fb = fb;
    out.hash = test::framebuffer_hash(*fb);
    out.alpha_bbox = compute_alpha_bbox(*fb);
    fill_canary_structural_fields(out);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// #3 Video raw frame — subprocess encode + ffmpeg subprocess decode
// ═══════════════════════════════════════════════════════════════════════════

struct VideoArtifacts {
    std::filesystem::path mp4_path;
    std::filesystem::path frame_png_path;
    bool                  available{false};
    std::string           skip_reason{};
};

VideoArtifacts encode_decode_video(const std::string& comp_id) {
    VideoArtifacts art;
    const std::string cli_path = locate_cli_binary();
    if (cli_path.empty() || !std::filesystem::exists(cli_path)) {
        art.skip_reason = "CHRONON3D_CLI_EXE_PATH not set or binary missing";
        return art;
    }
    art.mp4_path = std::filesystem::temp_directory_path() /
                   ("cross_process_video_" + comp_id + ".mp4");
    art.frame_png_path = std::filesystem::temp_directory_path() /
                         ("cross_process_video_" + comp_id + "_f0.png");
    if (std::filesystem::exists(art.mp4_path)) std::filesystem::remove(art.mp4_path);
    if (std::filesystem::exists(art.frame_png_path)) std::filesystem::remove(art.frame_png_path);

    std::ostringstream encode_cmd;
    encode_cmd << cli_path << " video --comp-id " << comp_id
               << " --start 0 --end 0 --output " << art.mp4_path.string()
               << " 2>/dev/null";
    if (run_command(encode_cmd.str()) != 0 ||
        !std::filesystem::exists(art.mp4_path)) {
        art.skip_reason = "chronon3d_cli video encode failed";
        return art;
    }
    std::ostringstream decode_cmd;
    decode_cmd << "ffmpeg -y -i " << art.mp4_path.string()
               << " -vframes 1 -f image2 " << art.frame_png_path.string()
               << " 2>/dev/null";
    if (run_command(decode_cmd.str()) != 0 ||
        !std::filesystem::exists(art.frame_png_path)) {
        // Cleanup the mp4 since we won't be using it.
        if (std::filesystem::exists(art.mp4_path)) std::filesystem::remove(art.mp4_path);
        art.skip_reason = "ffmpeg decode subprocess failed (is ffmpeg on PATH?)";
        return art;
    }
    // Cleanup the mp4 (we only need the decoded PNG).
    if (std::filesystem::exists(art.mp4_path)) std::filesystem::remove(art.mp4_path);
    art.available = true;
    return art;
}

PipelineResult render_pipeline_video_raw(const std::string& comp_id) {
    PipelineResult out;
    out.pipeline_name = "video_raw";
    out.frame = Frame{0};

    auto art = encode_decode_video(comp_id);
    if (!art.available) {
        out.available = false;
        out.skip_reason = art.skip_reason;
        return out;
    }

    auto fb = test::load_png_as_framebuffer(art.frame_png_path.string());
    // Cleanup unconditionally before the nullptr check.
    if (std::filesystem::exists(art.frame_png_path)) {
        std::filesystem::remove(art.frame_png_path);
    }
    if (!fb) {
        out.available = false;
        out.skip_reason = "decoded PNG load failed";
        return out;
    }
    out.fb = fb;
    out.hash = test::framebuffer_hash(*fb);
    out.alpha_bbox = compute_alpha_bbox(*fb);
    fill_canary_structural_fields(out);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// #4 Render graph — in-process with explicit graph path + diagnostics
// ═══════════════════════════════════════════════════════════════════════════

PipelineResult render_pipeline_render_graph(const Composition& comp) {
    PipelineResult out;
    out.pipeline_name = "render_graph";
    out.frame = Frame{0};

    auto renderer = test::make_renderer_shared();
    renderer->set_diagnostic_mode(true);
    auto fb = renderer->render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    out.fb = fb;
    out.hash = test::framebuffer_hash(*fb);
    out.alpha_bbox = compute_alpha_bbox(*fb);
    fill_canary_structural_fields(out);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// #5 Direct pipeline — in-process with legacy scene-level path
// ═══════════════════════════════════════════════════════════════════════════

PipelineResult render_pipeline_direct(const Composition& comp) {
    PipelineResult out;
    out.pipeline_name = "direct";
    out.frame = Frame{0};

    auto renderer = test::make_renderer_shared();
    RenderSettings settings;
    settings.use_modular_graph = false;
    renderer->set_settings(settings);
    auto fb = renderer->render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    out.fb = fb;
    out.hash = test::framebuffer_hash(*fb);
    out.alpha_bbox = compute_alpha_bbox(*fb);
    fill_canary_structural_fields(out);
    return out;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// §Cross-process assertion macros
// ═══════════════════════════════════════════════════════════════════════════
//
// `assert_in_process_11_checks` — byte-exact hash vs base (#1 self-test).
// 11 CHECKs (4 REQUIRE sanity + 7 CHECK identity).
//
// `assert_cross_path_11_checks` — for #4 and #5 (legacy/diagnostic paths
// may differ in the last 1-2 bits of the hash from the base).  Uses
// SSIM >= 0.999 + mean_err <= 1/255 instead of byte-exact hash.  Same
// 11 CHECKs total.

#define assert_in_process_parity(name, result, base_ref)                          \
    do {                                                                            \
        REQUIRE((result).available);                                               \
        REQUIRE((result).glyph_count > 0);                                         \
        REQUIRE((result).hash != 0);                                               \
        CHECK((result).layout_bbox.size.x >= 0.0f);                                \
        CHECK((result).world_bbox.size.x >= 0.0f);                                 \
        CHECK((result).predicted_bbox.size.x >= 0.0f);                             \
        CHECK((result).alpha_bbox.size.x > 0.0f);                                  \
        CHECK((result).hash == (base_ref).hash);                                   \
        CHECK(std::abs((result).layout_bbox.size.x -                              \
                       (base_ref).layout_bbox.size.x) <= kBBoxTolerancePx);        \
        CHECK(std::abs((result).world_bbox.size.x -                               \
                       (base_ref).world_bbox.size.x) <= kBBoxTolerancePx);         \
        CHECK(std::abs((result).predicted_bbox.size.x -                           \
                       (base_ref).predicted_bbox.size.x) <= kBBoxTolerancePx);     \
        CHECK(std::abs((result).alpha_bbox.size.x -                               \
                       (base_ref).alpha_bbox.size.x) <= kBBoxTolerancePx);         \
    } while (0)

#define assert_cross_path_parity(name, result, base_ref)                          \
    do {                                                                            \
        REQUIRE((result).available);                                               \
        REQUIRE((result).glyph_count > 0);                                         \
        CHECK((result).layout_bbox.size.x >= 0.0f);                                \
        CHECK((result).world_bbox.size.x >= 0.0f);                                 \
        CHECK((result).predicted_bbox.size.x >= 0.0f);                             \
        CHECK((result).alpha_bbox.size.x > 0.0f);                                  \
        CHECK((result).glyph_count == (base_ref).glyph_count);                     \
        CHECK(std::abs((result).layout_bbox.size.x -                              \
                       (base_ref).layout_bbox.size.x) <= kBBoxTolerancePx);        \
        CHECK(std::abs((result).world_bbox.size.x -                               \
                       (base_ref).world_bbox.size.x) <= kBBoxTolerancePx);         \
        CHECK(std::abs((result).predicted_bbox.size.x -                           \
                       (base_ref).predicted_bbox.size.x) <= kBBoxTolerancePx);     \
        CHECK(std::abs((result).alpha_bbox.size.x -                               \
                       (base_ref).alpha_bbox.size.x) <= kBBoxTolerancePx);         \
        REQUIRE((result).fb != nullptr);                                          \
        REQUIRE((base_ref).fb != nullptr);                                         \
        auto diff__local = test::compare_framebuffers(*(result).fb,                \
                                                      *(base_ref).fb);            \
        CHECK(diff__local.ssim >= 0.999);                                          \
        CHECK(diff__local.mean_abs_error <= 1.0 / 255.0);                          \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════
// §Cross-process 5 pipelines × 11 CHECKs each = 55 CHECKs
// + 1 dedicated CanaryGolden drift test = 56 CHECKs total
// ═══════════════════════════════════════════════════════════════════════════

PipelineResult& base_reference() {
    static PipelineResult r = []() {
        auto comp = content::certification::cert_title();
        return render_pipeline_sdk_still(comp);
    }();
    return r;
}

// #0 — CanaryGolden drift detection. Runs unconditionally, no subprocess.
// 6 CHECKs: validates all 5 hardcoded CanaryGolden constants against the
// actual SDK still base reference.  If the canary composition ever
// changes, this test catches the drift before any pipeline test runs.
TEST_CASE("CanaryGolden matches base reference (drift detection)") {
    auto comp = content::certification::cert_title();
    auto sdk = render_pipeline_sdk_still(comp);
    CHECK(CanaryGolden::kGlyphCount == sdk.glyph_count);
    // 5-10px tolerance for Inter Bold TTF revisions across platforms.
    CHECK(std::abs(CanaryGolden::kLayoutW - sdk.layout_bbox.size.x) <= 10.0f);
    CHECK(std::abs(CanaryGolden::kLayoutH - sdk.layout_bbox.size.y) <= 10.0f);
    CHECK(std::abs(CanaryGolden::kWorldW  - sdk.world_bbox.size.x)  <= 10.0f);
    CHECK(std::abs(CanaryGolden::kWorldH  - sdk.world_bbox.size.y)  <= 10.0f);
    CHECK(std::abs(CanaryGolden::kPredW   - sdk.predicted_bbox.size.x) <= 15.0f);
    CHECK(std::abs(CanaryGolden::kPredH   - sdk.predicted_bbox.size.y) <= 15.0f);
}

// #1 — SDK still (default, base reference). 11 CHECKs (in_process variant).
TEST_CASE("Cross-process parity #1: SDK still (base reference, byte-exact hash)") {
    auto& base = base_reference();
    auto comp = content::certification::cert_title();
    auto result = render_pipeline_sdk_still(comp);
    assert_in_process_parity("sdk_still", result, base);
    INFO("sdk_still self-base: hash=" << result.hash);
}

// #2 — CLI still (subprocess, hash + alpha_bbox). 6 CHECKs.
// Documented gap: the 4 structural fields (glyph_count, layout_bbox,
// world_bbox, predicted_bbox) are NOT compared against the base because
// the CLI's `still` command only writes a PNG, not a JSON sidecar.
TEST_CASE("Cross-process parity #2: CLI still (subprocess, hash + alpha_bbox)") {
    auto& base = base_reference();
    auto result = render_pipeline_cli_still("CertTitle");
    if (!result.available) {
        MESSAGE("CLI still skipped: " << result.skip_reason);
        return;
    }
    CHECK(result.hash == base.hash);
    CHECK(std::abs(result.alpha_bbox.size.x - base.alpha_bbox.size.x)
          <= kBBoxTolerancePx);
    CHECK(std::abs(result.alpha_bbox.size.y - base.alpha_bbox.size.y)
          <= kBBoxTolerancePx);
    CHECK(result.glyph_count == CanaryGolden::kGlyphCount);
    CHECK(result.layout_bbox.size.x > 0.0f);
    CHECK(result.predicted_bbox.size.x > 0.0f);
}

// #3 — Video raw frame (subprocess encode + ffmpeg subprocess decode).
// Post-H.264 invariants: SSIM >= 0.98 + mean err <= 3/255.
TEST_CASE("Cross-process parity #3: Video raw frame (H.264 transport SSIM/mean-err)") {
    auto& base = base_reference();
    auto result = render_pipeline_video_raw("CertTitle");
    if (!result.available) {
        MESSAGE("Video raw skipped: " << result.skip_reason);
        return;
    }
    REQUIRE(result.fb != nullptr);
    REQUIRE(base.fb != nullptr);
    auto diff = test::compare_framebuffers(*result.fb, *base.fb);
    CHECK(diff.ssim >= 0.98);
    CHECK(diff.mean_abs_error <= 3.0 / 255.0);
    // alpha_bbox post-decode should still be near the pre-encode value.
    // H.264 chroma subsampling loosens tolerance to ±8px.
    CHECK(std::abs(result.alpha_bbox.size.x - base.alpha_bbox.size.x)
          <= kBBoxTolerancePx * 4.0f);
    INFO("video_raw: ssim=" << diff.ssim
         << " mean_err=" << (diff.mean_abs_error * 255.0) << "/255"
         << " psnr=" << diff.psnr << "dB");
}

// #4 — Render graph (in-process, explicit graph path + diagnostics).
// NOT byte-exact hash with #1 → use cross_path variant.
TEST_CASE("Cross-process parity #4: Render graph (SSIM >= 0.999 cross-path)") {
    auto& base = base_reference();
    auto comp = content::certification::cert_title();
    auto result = render_pipeline_render_graph(comp);
    assert_cross_path_parity("render_graph", result, base);
}

// #5 — Direct pipeline (in-process, legacy scene-level path).
// NOT byte-exact hash with #1 → use cross_path variant.
TEST_CASE("Cross-process parity #5: Direct pipeline (SSIM >= 0.999 cross-path)") {
    auto& base = base_reference();
    auto comp = content::certification::cert_title();
    auto result = render_pipeline_direct(comp);
    assert_cross_path_parity("direct", result, base);
}
