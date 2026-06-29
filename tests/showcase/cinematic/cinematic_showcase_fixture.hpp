// ═══════════════════════════════════════════════════════════════════════════
//  cinematic_showcase_fixture.hpp — AGENT 4 / TICKET-A4 frame-analysis harness.
//
//  Phase-2.2 mechanical split.  Hosts what was an anonymous-namespace block
//  in tests/showcase/test_cinematic_camera_showcase.cpp (was 771 LOC):
//
//    • Canonical key-frame indices      (kKeyFrames / kKFStatic)
//    • Composition native resolution    (kCompW / kCompH)
//    • Runtime key-frame slice          (runtime_kf())
//    • FrameMetrics struct              + compute_metrics()
//    • FNV-1a 64-bit Framebuffer hash   (hash_framebuffer)
//    • Diagnostic formatters            (hash_to_hex, stamped)
//    • FrameCache rendering helper      (render_frames — DECL only,
//                                       def in cinematic_showcase_fixture.cpp)
//
//  Per task brief, this header owns "creazione composizione, render frame,
//  calcolo metriche, confronto aspettative".  Runtime config (env-var driven
//  — g_runtime, CinematicConfig, read_cinematic_config) lives separately
//  in cinematic_showcase_config.hpp.  Authoring source files remain at
//  content/anims/compositions/cinematic_text_camera.hpp (umbrella header
//  unchanged in Phase 2.2; Phase 2.3 will keep that umbrella + add 5
//  per-composition .hpp/cpp siblings but does not break the test contract).
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cinematic_showcase_config.hpp"

#if defined(CHRONON3D_SOURCE_DIR)
#  define AGENT4_SOURCE_DIR CHRONON3D_SOURCE_DIR
#else
#  define AGENT4_SOURCE_DIR "."
#endif

namespace chronon3d::testing::cinematic {

// ── DoD key frames (canonical source-of-truth, NEVER reorder) ────────
// Per AGENT 4 brief: 0 / 30 / 70 / 110 / 145 / 179.
inline constexpr int kKeyFrames[] = {0, 30, 70, 110, 145, 179};
inline constexpr int kKFStatic    = sizeof(kKeyFrames) / sizeof(kKeyFrames[0]);

// Composition's native resolution.  Authors hard-code 1920×1080 in
// deep_parallax_cascade() — the harness respects that contract
// ("Non modificare il codice core degli agenti 1 e 2") and never overrides.
inline constexpr int kCompW = 1920;
inline constexpr int kCompH = 1080;

// ── Runtime-sliced view of kKeyFrames — first g_runtime.frame_count
// entries.  Cached lazily as a `static const` vector so the 4 split
// test TUs share one allocation across invocations.
inline const std::vector<int>& runtime_kf() {
    static const auto v = []() {
        std::vector<int> out;
        const int n = std::min<int>(kKFStatic,
                    ::chronon3d::testing::cinematic_cfg::g_runtime.frame_count);
        out.reserve(n);
        for (int i = 0; i < n; ++i) out.push_back(kKeyFrames[i]);
        return out;
    }();
    return v;
}

// ── Frame metrics struct ──────────────────────────────────────────────
// Aggregates everything the DoD gates need from a single rendered frame:
//   hash          — FNV-1a 64-bit over the logical byte view (deterministic).
//   ink_pixels    — pixels with alpha > 0.05 (covers transparency /
//                   partial-opacity key frames without false blanks).
//   alpha_cov     — ink_pixels / total_pixels.
//   mean_lum      — luminance averaged over ink_pixels.
//   render_ms     — wall-clock for THIS frame; nice-to-have telemetry.
struct FrameMetrics {
    std::uint64_t hash{0};
    std::uint64_t ink_pixels{0};
    float         alpha_coverage{0.0f};
    float         mean_luminance{0.0f};
    float         render_ms{0.0f};
};

// FNV-1a 64-bit over the Framebuffer's logical byte span.
// Framebuffer::bytes() returns a span over the active logical area
// (no cache-line stride padding), so byte-by-byte folding is safe
// and bit-exact across runs.
inline std::uint64_t hash_framebuffer(const Framebuffer& fb) {
    constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime  = 0x100000001b3ULL;
    std::uint64_t h = kOffset;
    const auto bytes = fb.bytes();
    for (auto b : bytes) {
        h ^= static_cast<std::uint64_t>(b);
        h *= kPrime;
    }
    return h;
}

inline FrameMetrics compute_metrics(const Framebuffer& fb, float render_ms) {
    FrameMetrics m;
    m.hash      = hash_framebuffer(fb);
    m.render_ms = render_ms;
    const int W = fb.width();
    const int H = fb.height();
    int ink = 0;
    double sum_a = 0.0;
    double sum_l = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Color c = fb.get_pixel(x, y);
            if (c.a > 0.05f) {
                ++ink;
                sum_a += c.a;
                sum_l += 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            }
        }
    }
    const int total_px = (W > 0 && H > 0) ? (W * H) : 0;
    m.ink_pixels     = static_cast<std::uint64_t>(ink);
    m.alpha_coverage = (total_px > 0)
                           ? static_cast<float>(ink) / static_cast<float>(total_px)
                           : 0.0f;
    m.mean_luminance = ink > 0 ? static_cast<float>(sum_l / ink) : 0.0f;
    return m;
}

// Diagnostic formatters (cheap; emitted by doctest failure messages
// or under `-DCTEST_VERBOSE`).
inline std::string hash_to_hex(std::uint64_t h) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "0x%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}

inline std::string stamped(int frame) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "F%03d", frame);
    return std::string(buf);
}

using FrameRow   = std::pair<FrameMetrics, std::shared_ptr<Framebuffer>>;
using FrameCache = std::map<int, FrameRow>;

// Render the runtime-configured keyframes against a fresh renderer.
// Each row of the FrameCache owns its Framebuffer so the lifetime is
// safe across multiple gates (A4.5 contact sheet reuses these buffers
// when present).  Declaration only — definition lives in
// cinematic_showcase_fixture.cpp so the inner REQUIRE assertions
// don't shadow the test FILE macros in 4 separate TUs.
FrameCache render_frames(SoftwareRenderer& renderer, const Composition& comp);

} // namespace chronon3d::testing::cinematic
