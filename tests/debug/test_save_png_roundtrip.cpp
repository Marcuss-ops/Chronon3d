// =============================================================================
// tests/debug/test_save_png_roundtrip.cpp — TICKET-GATE-10-PHASE-4-BLACK repro
// =============================================================================
//
// Minimal reproducibility test for the black-render PNG bug flagged by the
// install_consumer gate #10 (Phase 4).  Strategy:
//   (1) Create a 64x64 Framebuffer filled with white pixels (linear 1,1,1,1)
//   (2) Call chronon3d::save_png() to /tmp/test_save_png_white.png
//   (3) Reload the PNG via stbi_load() (header-only include, symbols come
//       from chronon3d_backend_image already linked via chronon3d_pipeline)
//   (4) Verify ALL 64*64 reloaded pixels are (255, 255, 255, 255)
//
// This decouples the WRITE path (save_png) from the READ path (stbi_load).
// If the in-memory Framebuffer is white but the reloaded file is black,
// the bug is in save_png.  If the reloaded file matches the in-memory
// representation, the bug is in the consumer-side pixel verification
// (PIL / Python).
//
// Framework: standalone doctest-free diagnostic (gated by /tmp file output,
// not ctest registration).  Returns 0 if roundtrip is pixel-perfect, !=0
// otherwise.  Logs every step to stdout for forensic analysis.
//
// Compile contract: this file MUST NOT define STB_IMAGE_IMPLEMENTATION —
// the implementation is already provided by src/backends/image/stb_image_backend.cpp
// (OBJECT library chronon3d_backend_image, aggregated by chronon3d_pipeline).
//
// AGENTS.md V0.1 freeze-compliant: cat-1 diagnostic test, TIER=UNIT.
// =============================================================================

#include <chronon3d/backends/image/image_writer.hpp>
#include <cstdint>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types/types.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// header-only include — implementation comes from chronon3d_backend_image
#include <stb_image.h>

namespace {

constexpr const char* kPngPath = "/tmp/test_save_png_white.png";
constexpr std::int32_t kWidth  = 64;
constexpr std::int32_t kHeight = 64;

void log_step(const char* msg) noexcept {
    std::printf("[step] %s\n", msg);
    std::fflush(stdout);
}

}  // namespace

int main() {
    using namespace chronon3d;

    std::printf("=== save_png roundtrip diagnostic — white 64x64 Framebuffer ===\n");
    std::fflush(stdout);

    // ─── Step 1: Create white Framebuffer ───────────────────────────────
    log_step("Creating 64x64 Framebuffer filled with linear white (1,1,1,1)");
    Framebuffer fb(kWidth, kHeight);
    fb.clear(Color::transparent());

    for (i32 y = 0; y < kHeight; ++y) {
        for (i32 x = 0; x < kWidth; ++x) {
            fb.set_pixel(x, y, Color::white());
        }
    }

    const Color center = fb.get_pixel(kWidth / 2, kHeight / 2);
    std::printf("  in-memory center pixel: r=%.4f g=%.4f b=%.4f a=%.4f\n",
                center.r, center.g, center.b, center.a);
    std::fflush(stdout);

    // ─── Step 2: save_png ───────────────────────────────────────────────
    log_step("Calling chronon3d::save_png(...) to /tmp/test_save_png_white.png");
    const bool write_ok = save_png(fb, kPngPath);
    std::printf("  save_png returned: %s\n", write_ok ? "true" : "false");
    if (!write_ok) {
        std::fprintf(stderr, "FAIL: save_png returned false\n");
        return 1;
    }

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(kPngPath, ec);
    std::printf("  file size: %lld bytes\n", static_cast<long long>(file_size));
    std::fflush(stdout);

    if (file_size < 64) {
        std::fprintf(stderr, "FAIL: PNG file is suspiciously small (%lld bytes)\n",
                     static_cast<long long>(file_size));
        return 2;
    }

    // ─── Step 3: Read PNG back with stbi_load ───────────────────────────
    log_step("Reading PNG back via stbi_load (FREE decoder provided by chronon3d_backend_image)");
    int w2 = 0, h2 = 0, ch2 = 0;
    unsigned char* buffer = stbi_load(kPngPath, &w2, &h2, &ch2, 4);
    if (!buffer) {
        std::fprintf(stderr, "FAIL: stbi_load failed: %s\n",
                     stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return 3;
    }
    std::printf("  reloaded: %dx%d, requested 4 channels\n", w2, h2);
    std::fflush(stdout);

    if (w2 != kWidth || h2 != kHeight) {
        std::fprintf(stderr, "FAIL: dimension mismatch (got %dx%d, expected %dx%d)\n",
                     w2, h2, kWidth, kHeight);
        stbi_image_free(buffer);
        return 4;
    }

    // ─── Step 4: Verify ALL pixels are white ────────────────────────────
    log_step("Verifying ALL 64*64 pixels are (255,255,255,255)");
    int total         = w2 * h2;
    int bad_count     = 0;
    int sampled_black = 0;
    int sampled_other = 0;
    unsigned char first_bad_r = 0, first_bad_g = 0, first_bad_b = 0, first_bad_a = 0;
    int first_bad_idx = -1;

    for (int y = 0; y < h2; ++y) {
        for (int x = 0; x < w2; ++x) {
            const int i = (y * w2 + x) * 4;
            const unsigned char r = buffer[i + 0];
            const unsigned char g = buffer[i + 1];
            const unsigned char b = buffer[i + 2];
            const unsigned char a = buffer[i + 3];
            if (r == 255 && g == 255 && b == 255 && a == 255) continue;
            if (r == 0 && g == 0 && b == 0) {
                ++sampled_black;
            } else {
                ++sampled_other;
            }
            if (first_bad_idx < 0) {
                first_bad_idx = i / 4;
                first_bad_r = r; first_bad_g = g;
                first_bad_b = b; first_bad_a = a;
            }
            ++bad_count;
        }
    }

    std::printf("  total pixels: %d\n", total);
    std::printf("  white pixels: %d\n", total - bad_count);
    std::printf("  bad pixels:   %d\n", bad_count);
    std::printf("    fully-black: %d\n", sampled_black);
    std::printf("    other:       %d\n", sampled_other);
    if (first_bad_idx >= 0) {
        std::printf("  first bad pixel (linear idx %d): RGBA=(%u, %u, %u, %u)\n",
                    first_bad_idx,
                    static_cast<unsigned>(first_bad_r),
                    static_cast<unsigned>(first_bad_g),
                    static_cast<unsigned>(first_bad_b),
                    static_cast<unsigned>(first_bad_a));
    }
    std::fflush(stdout);

    stbi_image_free(buffer);

    if (bad_count == 0) {
        std::printf("PASS: roundtrip is pixel-perfect\n");
        return 0;
    }
    std::fprintf(stderr,
        "FAIL: %d/%d pixels wrong (black=%d, other=%d)\n"
        "  DIAGNOSIS: in-memory was white (1.0,1.0,1.0) but FILE has %s.\n"
        "  Likely root cause category: %s\n",
        bad_count, total, sampled_black, sampled_other,
        sampled_black == bad_count ? "all-black pixels" : "mixed non-white pixels",
        sampled_black == bad_count
            ? "WRITE-side: save_png writing zeros (or alpha-reset) for non-transparent pixels"
            : "WRITE-side: color channel swap or row-order inversion in save_png");
    return 5;
}
