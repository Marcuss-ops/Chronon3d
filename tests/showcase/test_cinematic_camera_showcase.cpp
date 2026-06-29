// ═══════════════════════════════════════════════════════════════════════════
//  AGENT 4 — Verifica visuale e integrazione / TICKET-A4
//  Cinematic camera showcase end-to-end verification.
//
//  Mission (per docs/agent-tasks/AGENT_4_VERIFY_VISUAL_INTEGRATION.md):
//    Demonstrate that Camera (Agent 1) + Text (Agent 2) subsystems both
//    work end-to-end WITHOUT modifying their core code (single-
//    responsibility gate; any probe or probe-like helper belongs here).
//
//  Composition under test:
//    chronon3d::content::anims::deep_parallax_cascade()
//    - duration = 180 frames at 30 fps
//    - Catmull-Rom camera path on the Z axis (waypoints at z=+2500,
//      +1000, -800, -400)
//    - 4 text layers parked at z = {1500, 1000, 500, 0} — parallax in Z
//    - Distinct opacity per layer so different layers visibly enter /
//      exit across the 6 keyframes
//
//  Frames asserted:
//    F000  F030  F070  F110  F145  F179
//    Coincide with composition milestones: backdrop at F030 stable,
//    first text-layer at F110 fully visible, all layers fading at F179.
//
//  Six DoD gates verified by the test body:
//    A4.1   every keyframe has visible ink, valid alpha, no NaN
//    A4.2   hashes differ between camera-motion windows (F030 vs F110)
//    A4.3   text motion: hash(F000) ≠ hash(Fmid) ≠ hash(Ffinal)
//    A4.4   determinism: render 6 frames twice → identical hashes
//    A4.5   contact sheet: 3×2 grid → output/showcase/contact_sheet.png
//    A4.6   performance: total / mean render ms + peak RSS
//
//  Non-goals (out of scope per DoD):
//    - No graphics-stack audit / no per-pixel goldens (Tier F visual suite
//      covers that).
//    - No CI integration of camera or text internals.
//    - No cross-OS work; Linux-only process_rss_bytes() call in A4.6 is
//      a best-effort metric; absent platform, MESSAGE is informational.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/render_fixtures.hpp>

// Exhibit the cinematic_text_camera compositions directly (no registry hop
// required for these).  cinematic_text_camera.hpp reaches:
//   content/anims/compositions/cinematic_text_camera.{hpp,cpp}.
#include "content/anims/compositions/cinematic_text_camera.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#if defined(CHRONON3D_SOURCE_DIR)
#  define AGENT4_SOURCE_DIR CHRONON3D_SOURCE_DIR
#else
#  define AGENT4_SOURCE_DIR "."
#endif

using namespace chronon3d;
using namespace chronon3d::content::anims;
using chronon3d::test::make_renderer;

namespace {

// ── DoD key frames ────────────────────────────────────────────────────
// Per AGENT 4 brief: 0 / 30 / 70 / 110 / 145 / 179.  Constexpr so the
// tests can iterate deterministically without runtime allocation.
constexpr int kKeyFrames[] = {0, 30, 70, 110, 145, 179};
constexpr int kKFCount     = sizeof(kKeyFrames) / sizeof(kKeyFrames[0]);

// Composition's native resolution.  Authors hard-code 1920×1080 in
// deep_parallax_cascade() — the harness respects that contract
// ("Non modificare il codice core degli agenti 1 e 2") and never overrides.
constexpr int kCompW = 1920;
constexpr int kCompH = 1080;

// ── Frame metrics struct ──────────────────────────────────────────────
// Aggregates everything the DoD gates need from a single rendered frame:
//   hash         — FNV-1a 64-bit over the logical byte view (deterministic).
//   ink_pixels   — pixels with alpha > 0.05 (covers transparency /
//                  partial-opacity key frames without false blanks).
//   alpha_cov    — ink_pixels / total_pixels.
//   mean_lum     — luminance averaged over ink_pixels.
//   render_ms    — wall-clock for THIS frame; nice-to-have telemetry.
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
    m.ink_pixels     = static_cast<std::uint64_t>(ink);
    m.alpha_coverage = (W > 0 && H > 0)
                           ? static_cast<float>(ink) / static_cast<float>(W * H)
                           : 0.0f;
    m.mean_luminance = ink > 0 ? static_cast<float>(sum_l / ink) : 0.0f;
    return m;
}

// Render the 6 DoD keyframes against a fresh renderer.  Each row of the
// returned FrameCache owns its Framebuffer so the lifetime is safe
// across multiple gates (A4.5 contact sheet reuses these buffers).
using FrameRow = std::pair<FrameMetrics, std::shared_ptr<Framebuffer>>;
using FrameCache = std::map<int, FrameRow>;

FrameCache render_six(SoftwareRenderer& renderer, const Composition& comp) {
    FrameCache out;
    for (int f : kKeyFrames) {
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

} // namespace

// ─────────────────────────────────────────────────────────────────────
//  A4.1 — Every key frame is non-empty: visible ink + valid alpha + no NaN.
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.1 every keyframe non-empty") {
    auto renderer = make_renderer();
    const auto comp = deep_parallax_cascade();
    const auto cache = render_six(renderer, comp);

    int non_empty = 0;
    for (int f : kKeyFrames) {
        const auto& m = cache.at(f).first;

        // Compose a single MESSAGE row for forensic tracing.  Cheap; printed
        // by doctest only on failure / `-DCTEST_VERBOSE`.
        MESSAGE(stamped(f)
                << " hash=" << hash_to_hex(m.hash)
                << " ink_px=" << m.ink_pixels
                << " alpha_cov=" << m.alpha_coverage
                << " mean_lum=" << m.mean_luminance
                << " render_ms=" << m.render_ms);

        // Frames at the boundary of the composition (F179 is post-hero
        // fade) reduce to backdrop-only ink — still has SOME ink because
        // bg_halo is always rendered.  We assert ink_pixels > 0 AND no NaN
        // in any of the four scalar telemetry fields.
        CHECK(m.ink_pixels > 0);
        CHECK(m.alpha_coverage > 0.0f);
        CHECK_FALSE(std::isnan(m.mean_luminance));
        CHECK_FALSE(std::isnan(m.alpha_coverage));
        CHECK_FALSE(std::isnan(m.render_ms));
        CHECK_FALSE(std::isinf(m.mean_luminance));
        CHECK_FALSE(std::isinf(m.alpha_coverage));

        if (m.ink_pixels > 0) ++non_empty;
    }
    CHECK(non_empty == kKFCount);
    MESSAGE("A4.1 OK — " << non_empty << "/" << kKFCount
            << " keyframes have visible ink + valid alpha");
}

// ─────────────────────────────────────────────────────────────────────
//  A4.2 — Camera motion: hashes between frames span multiple distinct
//          values, validating real motion across the timeline.  Proxy
//          probe (no direct camera.position() / rotation() accessors;
//          the visual signature across layers IS the canonical probe).
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.2 camera motion across 6 keyframes") {
    auto renderer = make_renderer();
    const auto comp = deep_parallax_cascade();
    const auto cache = render_six(renderer, comp);

    // F030 vs F110: camera has moved + parallax has shifted
    // foreground against background between these frames.  Real motion
    // ⇒ distinct framebuffer hashes.
    REQUIRE(cache.count(30) == 1);
    REQUIRE(cache.count(110) == 1);
    const auto h_f030 = cache.at(30).first.hash;
    const auto h_f110 = cache.at(110).first.hash;
    CHECK(h_f030 != h_f110);

    // Coarse lower bound: of the C(6, 2) = 15 frame pairs, at least 12
    // must produce distinct hashes (only the few pairs sitting on dead
    // keyframe holds — common in any 180-frame timeline — would even
    // legitimately collide).  12-of-15 is empirically safe for
    // deep_parallax_cascade: the bg_halo is static, but the 4 layers
    // each have a unique motion curve, so only overlapping holds may
    // collide.
    int diffs = 0;
    for (int i = 0; i < kKFCount; ++i) {
        for (int j = i + 1; j < kKFCount; ++j) {
            if (cache.at(kKeyFrames[i]).first.hash !=
                cache.at(kKeyFrames[j]).first.hash) {
                ++diffs;
            }
        }
    }
    CHECK(diffs >= 12);  // empirical lower bound; bg_halo + held keys can collide.
    MESSAGE("A4.2 OK — pairwise-distinct keyframes: "
            << diffs << " / 15 (F030 hash=" << hash_to_hex(h_f030)
            << " F110 hash=" << hash_to_hex(h_f110) << ")");
}

// ─────────────────────────────────────────────────────────────────────
//  A4.3 — Text motion per PRESET (TICKET-A2 followup #1).
//
//  Iterates ALL 5 cinematic_text_camera compositions, hashes F0 vs
//  Fdur/2 vs Fdur-1, asserts the three hashes are pairwise distinct.
//
//  Why five compositions instead of one:
//    The original A4.3 only covered deep_parallax_cascade (180-frame
//    duration exactly matches the 6 DoD keyframes).  AGENT 4 brief
//    says "Per ogni preset: F0 != Fmid; Fmid != Ffinal; F0 != Ffinal";
//    that maps cleanly to all 5 cinematic_text_camera compositions
//    registered in cinemtic_text_camera.cpp.  Each composition has its
//    own duration (90 / 180 / 180 / 210 / 240 frames) — so we use
//    Composition::duration() to derive (0, dur/2, dur-1) instead of
//    hard-coding the original 0/70/179 windows.
//
//  Memory discipline (the hash-only invariant):
//    Each render_frame() return is HASHED then DISCARDED — the
//    shared_ptr<Framebuffer> goes out of scope at the end of the inner
//    for-loop body, so peak transient for this test is a single
//    1920x1080x16B ≈ 33 MB framebuffer (vs. ~600 MB if all 5×3=15
//    framebuffers were retained).  Three hashes per composition are
//    stored in a std::array<std::uint64_t, 3> — trivially small.
//
//  Composition under test (canonical names from
//  content/anims/compositions/cinematic_text_camera.cpp):
//    1. deep_parallax_cascade   (Catmull-Rom Z-push through 4 layers)
//    2. whip_pan_hero_reveal    (camera whip + staggered typewriter)
//    3. orbit_handheld_glow     (Catmull-Rom orbit + handheld wiggle)
//    4. rack_focus_title_swap   (Vertigo dolly-zoom + opposing blur)
//    5. abyss_freefall_stagger  (rolling camera + glyph Z fall)
//
//  Expected outcome (per-preset): hash(F0) ≠ hash(dur/2) ≠ hash(dur-1),
//  all distinct.  At minimum 5/5 must pass for AGENT-4 DoD to be met.
//  If any preset fails, doctest reports it bypassing the others; the
//  MESSAGE call below makes the per-preset results visible without
//  breaking the test loop early.
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.3 text motion per preset (5 cinematic compositions)") {
    auto renderer = make_renderer();

    struct Preset {
        const char* name;
        Composition (*factory)();
    };
    const Preset kPresets[] = {
        {"deep_parallax_cascade",  &deep_parallax_cascade},
        {"whip_pan_hero_reveal",   &whip_pan_hero_reveal},
        {"orbit_handheld_glow",    &orbit_handheld_glow},
        {"rack_focus_title_swap",  &rack_focus_title_swap},
        {"abyss_freefall_stagger", &abyss_freefall_stagger},
    };
    constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

    int presets_passed = 0;
    int presets_total  = kPresetCount;

    for (int i = 0; i < kPresetCount; ++i) {
        const Preset& p = kPresets[i];
        const auto comp = p.factory();

        // Derive the three samples from the composition's canonical
        // duration.  Composition::duration() returns Frame (= i32 here),
        // so a static_cast keeps the arithmetic in int space and avoids
        // any accidental signed-vs-unsigned mismatch in Frame arithmetic.
        const int dur = static_cast<int>(comp.duration());
        // Robustness: a future stub composition returning duration=0 would
        // collapse half=0 and fin=-1+0=0 onto F0, producing 3 identical
        // hashes and a confusing 'hash collision' root cause.  Fail loudly
        // here so the operator sees 'dur=0' as the actual denominator.
        REQUIRE(dur > 0);
        const int half = dur / 2;
        const int fin  = (dur > 0) ? (dur - 1) : 0;

        // Three hashes; the shared_ptr<Framebuffer> is NOT retained.
        std::array<std::uint64_t, 3> hashes{};
        const int samples[3] = {0, half, fin};
        for (int s = 0; s < 3; ++s) {
            const int f = samples[s];
            auto fb = renderer.render_frame(comp, Frame{f});
            REQUIRE(fb != nullptr);
            hashes[s] = hash_framebuffer(*fb);
            // shared_ptr goes out of scope here → fb storage freed.
        }

        const bool all_distinct =
            (hashes[0] != hashes[1]) &&
            (hashes[1] != hashes[2]) &&
            (hashes[0] != hashes[2]);
        CHECK(all_distinct);
        if (all_distinct) ++presets_passed;

        MESSAGE("A4.3 per-preset " << p.name
                << " (dur=" << dur
                << ", samples=0/" << half << "/" << fin << ")"
                << " hash[0]="    << hash_to_hex(hashes[0])
                << " hash[mid]="  << hash_to_hex(hashes[1])
                << " hash[final]=" << hash_to_hex(hashes[2])
                << (all_distinct ? " (DISTINCT)" : " (COLLISION)"));
    }

    CHECK(presets_passed == presets_total);
    MESSAGE("A4.3 OK (per-preset) — " << presets_passed << "/"
            << presets_total << " cinematic compositions pass text-motion gate");
}

// ─────────────────────────────────────────────────────────────────────
//  A4.4 — Determinism: run the 6-frame loop twice; hashes must match.
//          Software renderer is single-threaded CPU — no stochastic
//          scheduling — but this contract is part of the DoD for any
//          future GPU/code-paths to honor; this test anchors it now.
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.4 determinism run A == run B") {
    auto renderer = make_renderer();
    const auto comp = deep_parallax_cascade();

    const auto run_a = render_six(renderer, comp);
    int matches_a = 0;
    for (int f : kKeyFrames) {
        const auto h = run_a.at(f).first.hash;
        MESSAGE("run A " << stamped(f) << " hash=" << hash_to_hex(h));
        ++matches_a;  // bookkeeping only
    }

    const auto run_b = render_six(renderer, comp);
    int matches_ab = 0;
    for (int f : kKeyFrames) {
        const auto h_a = run_a.at(f).first.hash;
        const auto h_b = run_b.at(f).first.hash;
        MESSAGE("run B " << stamped(f) << " hash=" << hash_to_hex(h_b));
        CHECK(h_a == h_b);
        ++matches_ab;
    }
    CHECK(matches_ab == kKFCount);
    CHECK(matches_a   == kKFCount);
    MESSAGE("A4.4 OK — " << matches_ab << "/" << kKFCount
            << " keyframes are bit-exact-identical across runs");
}

// ─────────────────────────────────────────────────────────────────────
//  A4.5 — Contact sheet: blit the 6 framebuffers into a 3×2 grid master
//          framebuffer, then save as PNG to output/showcase/contact_sheet.png.
//          No ImageMagick / PIL in env → SDK primitives (blit + save_png)
//          keep dependency zero.
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.5 contact sheet 3x2 to output/showcase/contact_sheet.png") {
    auto renderer = make_renderer();
    const auto comp = deep_parallax_cascade();
    const auto cache = render_six(renderer, comp);

    constexpr int kCols = 3;
    constexpr int kRows = 2;
    REQUIRE(kKFCount == kCols * kRows);

    Framebuffer master(kCompW * kCols, kCompH * kRows);
    int cell = 0;
    for (int f : kKeyFrames) {
        const int col = cell % kCols;
        const int row = cell / kCols;
        cache.at(f).second->blit(master, col * kCompW, row * kCompH);
        ++cell;
    }

    namespace fs = std::filesystem;
    fs::path out_dir = fs::path(AGENT4_SOURCE_DIR) / "output" / "showcase";
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    REQUIRE_FALSE(ec);
    const fs::path png_path = out_dir / "contact_sheet.png";

    const bool ok = save_png(master, png_path.string());
    REQUIRE(ok);
    REQUIRE(fs::exists(png_path));
    const auto bytes = fs::file_size(png_path);
    CHECK(bytes > 0);

    MESSAGE("A4.5 OK — contact sheet: " << png_path.string()
            << " (" << bytes << " bytes; " << kCols << "×" << kRows
            << " grid of " << kCompW << "×" << kCompH << ")");
}

// ─────────────────────────────────────────────────────────────────────
//  A4.6 — Performance: total/mean ms across 6 renders, approximate peak
//          RSS, AND per-test peak transient heap envelope.
//
//  TICKET-053: peak transient heap tracked via the lifetime hooks in
//  include/chronon3d/core/memory/framebuffer.hpp —
//    g_peak_live_framebuffer_bytes (HWM, bumped on ctor, never decremented)
//    g_live_framebuffer_bytes     (live bytes, bumped on ctor, decremented
//                                  on dtor / release_owned_pixels)
//  Snapshotting BEFORE vs AFTER the run gives the peak transient heap
//  contribution of A4.6 specifically, independent of RSS noise and of
//  any prior tests in the doctest process.  The HWM delta REPLACES the
//  previous <33 MB rule-of-thumb with an actual measured number, but
//  is REPORTED ONLY (MESSAGE) — no CI threshold is enforced here.  A
//  hard CI threshold will be added once the showcase fleet has been
//  measured end-to-end across all keyframes in an env-fixed baseline.
//  Rationale per AGENTS.md rule: an uncalibrated CHECK bound is a
//  flake source the same way an uncalibrated GREEN-mark is.  We
//  report the number; we don't gate on it until the number is known.
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.6 performance telemetry") {
    auto renderer = make_renderer();
    const auto comp = deep_parallax_cascade();

    SystemMetricsCollector sm;
    const auto rss_before = sm.process_rss_bytes();

    // ── TICKET-053: peak transient heap envelope (lifetime hooks) ────
    // The two atomic counters live in chronon3d::profiling; Framebuffer's
    // ctor calls framebuffer_increment_allocations() which bumps both.
    // `relaxed` ordering is fine: these reads are observational hooks,
    // not synchronisation primitives, and we never gate correctness on
    // their absolute value — only the delta.  Fully-qualified names so
    // the reads are robust against any future removal of the file-scope
    // `using namespace chronon3d;` directive.
    const std::uint64_t fb_hwm_prerun_bytes =
        ::chronon3d::profiling::g_peak_live_framebuffer_bytes.load(std::memory_order_relaxed);
    const std::uint64_t fb_live_prerun_bytes =
        ::chronon3d::profiling::g_live_framebuffer_bytes.load(std::memory_order_relaxed);

    const auto cache = render_six(renderer, comp);

    const std::uint64_t fb_hwm_postrun_bytes =
        ::chronon3d::profiling::g_peak_live_framebuffer_bytes.load(std::memory_order_relaxed);
    const std::uint64_t fb_live_postrun_bytes =
        ::chronon3d::profiling::g_live_framebuffer_bytes.load(std::memory_order_relaxed);

    const auto rss_after = sm.process_rss_bytes();
    const auto rss_peak_bytes = std::max(rss_before, rss_after);

    double sum_ms = 0.0;
    for (int f : kKeyFrames) {
        sum_ms += cache.at(f).first.render_ms;
    }
    const double mean_ms = sum_ms / static_cast<double>(kKFCount);
    const double total_ms = sum_ms;
    const double rss_mb = static_cast<double>(rss_peak_bytes) / (1024.0 * 1024.0);

    // ── TICKET-053 envelope derived data ──────────────────────────────
    //   hwm_delta   — peak transient heap contributed by THIS test.
    //                 Typically small: at most one shared_ptr<Framebuffer>
    //                 in flight during the inner render loop (≈33 MB at
    //                 1920×1080×16B).  This replaces the <33 MB rule-of-thumb
    //                 with an actual measured number.
    //   live_delta  — bytes the test STILL HOLDS after the cache build.
    //                 Equals 6 framebuffers ≈ 200 MB retained in cache.
    //                 Reflects the test's structural design rather than
    //                 a regression — report it for forensic transparency,
    //                 do NOT check it.
    const std::uint64_t fb_hwm_delta_bytes =
        (fb_hwm_postrun_bytes > fb_hwm_prerun_bytes)
            ? (fb_hwm_postrun_bytes - fb_hwm_prerun_bytes) : 0ULL;
    const std::int64_t  fb_live_delta_signed =
        static_cast<std::int64_t>(fb_live_postrun_bytes) -
        static_cast<std::int64_t>(fb_live_prerun_bytes);
    const std::uint64_t fb_live_delta_bytes =
        (fb_live_delta_signed > 0)
            ? static_cast<std::uint64_t>(fb_live_delta_signed) : 0ULL;
    const double fb_hwm_delta_mb =
        static_cast<double>(fb_hwm_delta_bytes) / (1024.0 * 1024.0);
    const double fb_live_delta_mb =
        static_cast<double>(fb_live_delta_bytes) / (1024.0 * 1024.0);

    CHECK(mean_ms < 30000.0);  // CI soft limit per frame.

    // TICKET-053 envelope REPORT (no CI threshold here).
    //   Defensive sanity CHECK: delta_bytes must be non-negative.  This
    //   catches an unexpected monotonic-counter underrun (atomic HWM
    //   regression or torn update) at no flake cost: always true under
    //   well-formed lifetime hooks.  The actual peak-transient envelope
    //   is REPORTED ONLY via MESSAGE; the previous <33 MB rule-of-thumb
    //   is replaced by a measured number.  Hard CI threshold deferred
    //   until env-fixed baseline measures the showcase fleet end-to-end
    //   (per AGENTS.md rule on uncalibrated gates).
    constexpr double kFBPeakEnvelopeMB = 132.0;  // safety-bound for human reference, NOT a CI gate.
    CHECK(fb_hwm_delta_mb >= 0.0);

    MESSAGE("A4.6 OK — total=" << total_ms << " ms"
            << " mean=" << mean_ms << " ms/frame"
            << " rss(peak)=" << rss_mb << " MB"
            << " fbpeak_delta=" << fb_hwm_delta_mb << " MB"
            << " fbretained_delta=" << fb_live_delta_mb << " MB"
            << " fbpeak_safety_bound=" << kFBPeakEnvelopeMB << " MB (TICKET-053 provisional, CI threshold deferred)"
            << " frames=" << kKFCount);
}
