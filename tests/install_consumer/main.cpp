// tests/install_consumer/main.cpp
//
// ── Real render end-to-end SDK consumer (TICKET-011 + Fase 6) ──
//
// This is a STANDALONE consumer project — it does NOT share
// tests/CMakeLists.txt and does NOT link against in-tree targets.
// Its only dependency is the *installed* Chronon3D package.
//
// The consumer:
//   1. Creates a composition with text + grid + an ACTIVE camera
//      (position + non-zero rotation + non-zero zoom + look_at).
//   2. Optionally calls a chronon3d_text_core-only symbol directly,
//      gated by the build-time CHRONON3D_HAVE_TEXT_CORE define
//      produced by the CMake probe.  Skipped with a structured log
//      line when the SDK was built with CHRONON3D_ENABLE_TEXT=OFF.
//   3. Renders a frame via RenderEngine.
//   4. Saves the framebuffer as a PNG.
//   5. Runs a post-render pixel-count check via popen() to
//      `python3 + PIL` (preferred) or falls back to ImageMagick's
//      `identify -format mean*255`.  Hard-fails if the count is
//      below a minimum threshold (an all-black PNG would pass the
//      file-size check but fail this one).
//   6. Emits the boundary marker `[BOUNDARY-OK] …` and exits 0.
//
// Wired into CTest via the top-level CMakeLists.txt option
// CHRONON3D_BUILD_INSTALL_CONSUMER_TEST (enabled by the linux-ci
// preset).  Orchestrator: tools/install_consumer_test.sh runs the
// full configure -> build -> install -> consume -> run chain.

#include <chronon3d/chronon3d.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

/// Run a shell command via popen(), return stdout trimmed of trailing
/// whitespace (\n, \r, \t, space).  Returns empty optional when popen
/// fails or the command produces no output.
std::optional<std::string> run_capture(const char* cmd) {
    if (!cmd || !*cmd) return std::nullopt;
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return std::nullopt;
    char buf[256] = {0};
    std::string out;
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) out.append(buf);
    const int rc = pclose(pipe);
    if (rc != 0) return std::nullopt;
    while (!out.empty() &&
           (out.back() == '\n' || out.back() == '\r' ||
            out.back() == '\t' || out.back() == ' ')) {
        out.pop_back();
    }
    if (out.empty()) return std::nullopt;
    return out;
}

/// Count non-zero pixels in sdk_consumer_output.png using the
/// preferred Python+PIL probe; falls back to ImageMagick's mean
/// value (heuristic proxy).  Returns -1 when neither tool is
/// available.
///
/// The Python path uses `sum(... for p in img.getdata() if p !=
/// (0,0,0,0))` exactly as requested by the Fase-6 spec — it counts
/// any RGBA pixel whose 4-tuple differs from fully-transparent
/// black.  The ImageMagick path is a coarse fallback that estimates
/// non-zero coverage from the mean intensity.
long count_non_zero_pixels(const std::filesystem::path& png) {
    const std::string py = std::string(
        "python3 -c \"from PIL import Image; "
        "img=Image.open('") + png.string() + "'); "
        "print(sum(1 for p in img.getdata() if p != (0,0,0,0)))\"";
    auto py_out = run_capture(py.c_str());
    if (py_out) return std::atol(py_out->c_str());

    const std::string im = std::string(
        "identify -format \"%[fx:int(mean*w*h)]\" ") + png.string();
    auto im_out = run_capture(im.c_str());
    if (im_out) return std::atol(im_out->c_str());
    return -1;
}

constexpr long kMinNonZeroPixels = 1000;  // 640x360 frame; threshold

}  // namespace

int main() {
    // ── 1. Composition: grid + 2 text layers + ACTIVE camera ──────
    auto comp = chronon3d::composition(
        {.name = "SDKConsumerTest",
         .width = 640,
         .height = 360,
         .duration = 1},
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder s(ctx);

            // Background grid (independent of text_core)
            s.layer("bg", [](chronon3d::LayerBuilder& l) {
                l.grid_background("grid", chronon3d::GridBackgroundParams{
                    .size = {640.0f, 360.0f},
                    .bg_color = {0.02f, 0.02f, 0.06f, 1.0f},
                    .grid_color = {0.15f, 0.55f, 1.0f, 0.10f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            // Title text (via the high-level LayerBuilder.text path)
            s.layer("title", [](chronon3d::LayerBuilder& l) {
                l.position({0.0f, -20.0f, 0.0f});
                l.text("txt", {
                    .content = {.value = "Chronon3D SDK"},
                    .font = {.font_family = "sans-serif",
                             .font_size = 48.0f},
                    .layout = {.box = {600.0f, 80.0f},
                               .align = chronon3d::TextAlign::Center,
                               .vertical_align = chronon3d::VerticalAlign::Middle},
                    .appearance = {.color = chronon3d::Color{0.92f, 0.97f, 1.0f, 1.0f}},
                    .position = {0.0f, 0.0f, 0.0f}
                });
            });

            // Subtitle text
            s.layer("sub", [](chronon3d::LayerBuilder& l) {
                l.position({0.0f, 60.0f, 0.0f});
                l.text("sub", {
                    .content = {.value = "External Consumer OK"},
                    .font = {.font_family = "sans-serif",
                             .font_size = 22.0f},
                    .layout = {.box = {600.0f, 48.0f},
                               .align = chronon3d::TextAlign::Center,
                               .vertical_align = chronon3d::VerticalAlign::Middle},
                    .appearance = {.color = chronon3d::Color{0.62f, 0.78f, 1.0f, 1.0f}},
                    .position = {0.0f, 0.0f, 0.0f}
                });
            });

            // ACTIVE camera with non-zero rotation AND zoom
            // (covers the Fase-6 spec: "≥1 nodo camera ATTIVA
            // (rotazione/zoom non-zero)"); 0.18 rad ~= 10.3°.
            s.camera().enable(true)
                .position({0.0f, 50.0f, -800.0f})
                .zoom(800.0f)
                .rotation(0.18f)
                .look_at({0.0f, 0.0f, 0.0f});

            return s.build();
        });

    // ── 2. Render ────────────────────────────────────────────────
    chronon3d::RenderEngine engine;
    auto fb = engine.render(comp, 0);

    if (!fb) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] render_frame returned null\n");
        return 1;
    }

    // ── 2b. Explicit chronon3d_text_core probe (link-time assertion)
    //
    // The probe is gated by the CMake-generated
    // CHRONON3D_HAVE_TEXT_CORE define (0|1).  When 1, the SDK archive
    // contains the text_core .cpp compilation units and
    // glyph_atlas_lookup is callable.  When 0, the SDK was built with
    // CHRONON3D_ENABLE_TEXT=OFF and we skip with a structured log.
    //
    // This complements the Fase-5 canary gate: even if the archive
    // contains text_core, the consumer's invocation exercises the
    // public symbol path end-to-end at run time.
#ifdef CHRONON3D_HAVE_TEXT_CORE
    std::printf(
        "[BOUNDARY-LOG] text_core reachable: invoking glyph_atlas_lookup() "
        "from chronon3d_text_core\n");
    auto probe = chronon3d::glyph_atlas_lookup(
        std::string_view{}, std::uint32_t{0}, std::size_t{12});
    (void)probe;
#else
    std::printf(
        "[BOUNDARY-LOG] text_core NOT in SDK archive - explicit "
        "chronon3d_text_core node skipped (CHRONON3D_ENABLE_TEXT=OFF)\n");
#endif

    // ── 3. Save PNG ──────────────────────────────────────────────
    const std::filesystem::path output_path = "sdk_consumer_output.png";
    if (!chronon3d::save_png(*fb, output_path.string())) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] save_png failed\n");
        return 1;
    }

    // ── 3b. File-level pre-condition (cheap, kept for forensics) ──
    if (!std::filesystem::exists(output_path)) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] output PNG not found: %s\n",
                     output_path.c_str());
        return 1;
    }
    const auto file_size = std::filesystem::file_size(output_path);
    if (file_size == 0) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] output PNG is empty\n");
        return 1;
    }

    // ── 4. Post-render pixel-count check (Fase-6 spec) ───────────
    //
    // Counts non-zero RGBA pixels via Python+PIL (preferred) or
    // ImageMagick fallback.  An all-zero PNG would pass the file-size
    // check but fail this.  Threshold of 1000 keeps the consumer
    // robust against "all-black" misconfigurations.
    const long non_zero = count_non_zero_pixels(output_path);
    if (non_zero < 0) {
        std::fprintf(stderr,
            "[BOUNDARY-FAIL] pixel-count probe failed (need Python+PIL "
            "or ImageMagick `identify` on PATH)\n");
        return 1;
    }
    if (non_zero < kMinNonZeroPixels) {
        std::fprintf(stderr,
            "[BOUNDARY-FAIL] output PNG has only %ld non-zero pixels "
            "(need >= %ld) - empty/all-black render\n",
            non_zero, kMinNonZeroPixels);
        return 1;
    }

    // ── 5. Boundary marker ───────────────────────────────────────
    std::printf("[BOUNDARY-OK] SDK consumer rendered %dx%d PNG (%zu bytes, %ld non-zero pixels)\n",
                fb->width(), fb->height(),
                static_cast<size_t>(file_size), non_zero);
    return 0;
}
