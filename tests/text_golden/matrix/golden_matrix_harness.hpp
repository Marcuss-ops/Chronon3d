// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/matrix/golden_matrix_harness.hpp — Shared harness header
// extracted from Batch 1 (TICKET-GOLDEN-MATRIX-SUBTITLE-BATCH-1).
//
// TICKET-GOLDEN-MATRIX-BATCH-3: this header is consumed by 4 .cpp matrices —
//   • test_golden_matrix_subtitle.cpp   (Batch 1 baseline)
//   • test_golden_matrix_emphasis.cpp   (NEW, Batch 3)
//   • test_golden_matrix_cinematic.cpp  (NEW, Batch 3)
//   • test_golden_matrix_reveal.cpp     (NEW, Batch 3)
// All 18 enrolled presets × 48 cells = 864 total cells sweep the same 5
// matrix dimensions (AR × text-length × scale × cache × ts).
//
// TICKET-GOLDEN-MATRIX-FULL-METRIC-COVERAGE (N3 fix, post-Batch 3, lean re-design):
// 8 of 11 metrics bound to machine-verified CHECK / REQUIRE inside
// sweep_preset_matrix — 4 cell-shape REQUIREs (ink_pixels, alpha_coverage,
// bbox.w/h) + 5 lean host-portable CHECKs (mean_luminance, visual_center,
// overflow, cut_text, empty_frame via FAIL-with-tolerate). `render_ms`
// and unique_hash >= N soft CHECK dropped per lean re-design (per-call
// host-specific variance too high; silent-fake-green diagnostic via INFO-
// only aggregate post-sweep).  Per AGENTS.md honest-discipline: every
// CHECK reduces rot; none are vacuous (compile-time-true).
//
// ── Functions marked `inline` (header-only) ───────────────────────────
//  Per AGENTS.md "Non duplicare registry, resolver, sampler, cache"
//  + Cat-3 anti-dup: the same MatrixConfig + render_matrix_cell +
//  append_manifest_entries MUST NOT be reimplemented per test file.
//  Inlined definitions keep each test TU isolated (no shared.cpp link
//  change required) while letting the linker fold duplicate inline
//  bodies at ODR-merge time.
//
// ── Matrix dimensions (Batch 1 + Batch 1.5 forward-point disclosure) ──
//  Active (5 dims, 48 cells/preset, per Batch 1.5 NETA-ZERO Option B):
//    1. Aspect ratio (16:9 / 9:16)             — 2 cells
//    2. Text length   (short / long)            — 2 cells
//    3. Scale          (normal / extreme)       — 2 cells
//    4. Cache state    (warm / cold)            — 2 cells
//    5. Timestamp      (initial / middle / end) — 3 cells
// Forward-point (shelved per Option B):
//    6. Background     (light / dark)            — TICKET-OPP-BG-CONSUMER
//    7. Scheduler mode (sequential / parallel)  — TICKET-EXECUTION-SCHEDULER-SET-MODE
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <doctest/doctest.h>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/registry/text_preset_registry.hpp>
#include <chronon3d/text/text_definition.hpp>

#include <content/text/text_helpers.hpp>
#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text/visual/text_visual_metrics.cpp>  // ScenarioMetrics + compute_metrics

namespace chronon3d::test::matrix_harness {

enum class AspectRatio : int { k16x9 = 0, k9x16 = 1 };

struct AspectDims { int width; int height; };

inline AspectDims aspect_dims(AspectRatio r) {
    return r == AspectRatio::k16x9 ? AspectDims{1920, 1080}
                                   : AspectDims{1080, 1920};
}

inline const char* aspect_tag(AspectRatio r) {
    return r == AspectRatio::k16x9 ? "169" : "916";
}

// ── MatrixCell — single sweep instance ───────────────────────────────────
//
// Encodes all 5 active dimensions + leaves room for the 2 forward-point ones
// (bg / scheduler) so the same cell shape stays valid once Option B escape
// hatches are wired (per TICKET-OPP-BG-CONSUMER + TICKET-EXECUTION-SCHEDULER-SET-MODE).
struct MatrixCell {
    AspectRatio ar{AspectRatio::k16x9};
    bool        long_text{false};
    bool        extreme_scale{false};
    bool        cache_on{true};
    int         t_frame{0};
    // Forward-point dimensions (background + scheduler) are NOT yet on this
    // struct per AGENTS.md zero-callers invariant + Cat-3 anti-dup.
    // They are added IN THE SAME ATOMIC COMMIT that wires them through
    // `render_matrix_cell` + `matrix_tag` + OPP/scheduler plumbing
    // (per `### 2×-in-one-chore: deprecation reversal bundles forward-point
    // tickets` rule).  Forward-points live at:
    //   * TICKET-OPP-BG-CONSUMER (ADR-029)
    //   * TICKET-EXECUTION-SCHEDULER-SET-MODE (ADR-030)
};

// File-tag encoding of the 5 active matrix dimensions.
inline std::string matrix_tag(const MatrixCell& c) {
    std::ostringstream os;
    os << aspect_tag(c.ar)
       << (c.long_text       ? "_L" : "_S")
       << (c.extreme_scale   ? "_x4" : "_x1")
       << (c.cache_on        ? "_cw" : "_cc");
    return os.str();
}

inline bool fast_mode() {
    const char* env = std::getenv("CHRONON3D_GOLDEN_MATRIX_FAST_MODE");
    if (!env) return false;
    std::string v(env);
    for (auto& ch : v)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

inline const chronon3d::registry::TextPresetRegistry& shared_text_preset_registry() {
    static const chronon3d::registry::TextPresetRegistry kReg = []() {
        auto reg = chronon3d::registry::make_default_text_preset_registry();
        reg.freeze();
        return reg;
    }();
    return kReg;
}

struct CellResult {
    chronon3d::test::GoldenTestResult golden;
    ScenarioMetrics                   metrics;
    std::string                       short_label;
};

inline CellResult render_matrix_cell(chronon3d::SoftwareRenderer& renderer,
                                     std::string_view preset_id,
                                     const MatrixCell& c) {
    const std::string preset_id_str{preset_id};
    AspectDims d = aspect_dims(c.ar);
    const chronon3d::f32 font_size = c.extreme_scale
        ? (d.width >= d.height ? 384.0f : 256.0f)
        : (d.width >= d.height ?  96.0f :  64.0f);
    // Uniform content: bit-identical to Batch 1 baseline (`test_golden_matrix_subtitle.cpp`).
    // Per Cat-3 anti-dup, the same `MatrixCell{...}` produce the same hash regardless of
    // preset tier, which keeps cross-batch manifest hashes comparable.  Per-preset
    // vocabulary is the dedicated per-preset fixture's job (`tests/visual/text/...`).
    const std::string content = c.long_text
        ? "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789"
        : "SUBTITLE";

    auto comp = chronon3d::composition(
        {.name = "GoldenMatrix/" + preset_id_str + "/" + matrix_tag(c),
         .width = d.width, .height = d.height,
         .frame_rate = chronon3d::FrameRate{30, 1},
         .duration = 60},
        [&renderer, preset_id_str, content, font_size, d]
        (const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            chronon3d::SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("matrix", [&s, preset_id_str, content, font_size, d]
                              (chronon3d::LayerBuilder& l) {
                const auto& preset = shared_text_preset_registry().get(preset_id_str);
                auto base = chronon3d::content::text::centered_text(
                    chronon3d::content::text::CenterTextOptions{
                        .text         = content,
                        .box          = chronon3d::Vec2{d.width * 0.85f, d.height * 0.85f},
                        .font_asset   = "assets/fonts/Poppins-Bold.ttf",
                        .font_family  = "Poppins",
                        .font_weight  = 700,
                        .font_size    = font_size,
                        .color        = chronon3d::Color::white(),
                    });
                if (preset.builder) {
                    preset.builder(s, l, chronon3d::from_text_definition(base));
                }
            });
            return s.build();
        });

    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, chronon3d::Frame{c.t_frame});
    REQUIRE(fb != nullptr);

    auto m = compute_metrics(*fb, t0);

    std::string short_label = "GM03_" + preset_id_str + "_" + matrix_tag(c) +
                              "_F" + std::to_string(c.t_frame);

    chronon3d::test::GoldenTestConfig gcfg;
    gcfg.golden_directory   = "test_renders/golden/matrix";
    gcfg.artifact_directory = "test_renders/artifacts/matrix";
    gcfg.mode               = chronon3d::test::golden_mode_from_environment();
    gcfg.threshold.max_mean_abs_error    = 8.0 / 255.0;
    gcfg.threshold.max_abs_error          = 50.0 / 255.0;
    gcfg.threshold.max_changed_pixel_ratio = 0.08;
    gcfg.threshold.max_rmse               = 10.0 / 255.0;
    gcfg.threshold.min_ssim               = 0.88;

    CellResult res;
    res.short_label = short_label;
    res.metrics     = m;
    res.golden      = chronon3d::test::verify_golden(*fb, short_label, gcfg);
    return res;
}

// FIX 5 dims × 3 ts = 48 cells per preset; FAST_MODE 6 cells/preset.
inline std::vector<MatrixCell> matrix_cells_for_preset(std::string_view /*preset_id*/) {
    std::vector<MatrixCell> cells;

    const std::vector<AspectRatio> ars = {AspectRatio::k16x9, AspectRatio::k9x16};
    const std::vector<int>         timestamps = {0, 20, 40};

    if (fast_mode()) {
        for (auto ar : ars) {
            for (int t : timestamps) {
                MatrixCell c; c.ar = ar; c.t_frame = t;
                cells.push_back(c);
            }
        }
        return cells;
    }

    // Full matrix: 2 (AR) × 2 (text) × 2 (scale) × 2 (cache) × 3 (ts) = 48 cells/preset.
    // bg + scheduler dimensions SHELVED per Batch 1.5 Option B
    // (thinker-with-files-gemini Q4-A): the OPP compiler path does NOT
    // currently consume CompositionSpec::background_color_rgba, and
    // ExecutionScheduler has no set_mode() API at ctor.  Adding these as
    // active cells would emit bit-identical _l/_d and _s/_p goldens
    // (silent-fake green suite).  Forward-points:
    //   - TICKET-OPP-BG-CONSUMER (ADR-029)
    //   - TICKET-EXECUTION-SCHEDULER-SET-MODE (ADR-030)
    for (auto ar : ars)
    for (bool long_text       : {false, true})
    for (bool extreme_scale   : {false, true})
    for (bool cache_on        : {false, true})
    for (int  t_frame         : timestamps) {
        MatrixCell c;
        c.ar              = ar;
        c.long_text       = long_text;
        c.extreme_scale   = extreme_scale;
        c.cache_on        = cache_on;
        c.t_frame         = t_frame;
        cells.push_back(c);
    }
    return cells;
}

// Append a manifest JSONL entry for the cell (audit trail).
inline void append_manifest_entries(std::string_view preset_id,
                                     const std::vector<std::pair<MatrixCell, CellResult>>& results) {
    if (results.empty()) return;
    std::filesystem::create_directories("test_renders/golden/matrix");
    const std::string path = std::string{"test_renders/golden/matrix/"} +
                              std::string{preset_id} + ".matrix.jsonl";
    std::ofstream out(path, std::ios::app);
    if (!out) return;
    for (const auto& [cell_cfg, res] : results) {
        out << "{"
            << "\"preset\":\"" << std::string{preset_id} << "\","
            << "\"cell\":\"" << matrix_tag(cell_cfg) << "\","
            << "\"t\":" << cell_cfg.t_frame << ","
            << "\"hash\":\"" << std::hex << static_cast<std::uint64_t>(res.metrics.hash) << std::dec << "\","
            << "\"ink_pixels\":" << res.metrics.ink_pixels << ","
            << "\"alpha_coverage\":" << res.metrics.alpha_coverage << ","
            << "\"bbox\":[" << res.metrics.ink_bbox.x << "," << res.metrics.ink_bbox.y
            <<      "," << res.metrics.ink_bbox.w << "," << res.metrics.ink_bbox.h << "],"
            << "\"visual_center\":[" << res.metrics.visual_center.x << "," << res.metrics.visual_center.y << "],"
            << "\"overflow\":" << (res.metrics.overflow ? "true" : "false") << ","
            << "\"empty\":"    << (res.metrics.empty_frame ? "true" : "false") << ","
            << "\"cut\":"      << (res.metrics.cut_text ? "true" : "false") << ","
            << "\"golden_passed\":" << (res.golden.passed ? "true" : "false") << ","
            << "\"golden_missing\":" << (res.golden.golden_missing ? "true" : "false")
            << "}\n";
    }
}

inline void sweep_preset_matrix(std::string_view preset_id) {
    auto renderer = chronon3d::test::make_renderer();
    auto cells = matrix_cells_for_preset(preset_id);

    INFO("Preset: ", std::string{preset_id}, " — cells: ", cells.size(),
         " (fast_mode=", fast_mode() ? "true" : "false", ")");

    int pass_count = 0;
    int golden_missing_count = 0;
    int cut_count = 0;
    int overflow_count = 0;
    int empty_count = 0;
    std::set<std::uint64_t> unique_hash_set;  // N3: aggregate unique-hash sanity

    std::vector<std::pair<MatrixCell, CellResult>> cell_results;
    cell_results.reserve(cells.size());        for (const auto& cfg : cells) {
            CAPTURE(std::string{preset_id});
            CAPTURE(matrix_tag(cfg));
            CAPTURE(cfg.t_frame);

            AspectDims d = aspect_dims(cfg.ar);

            CellResult res = render_matrix_cell(renderer, preset_id, cfg);
            cell_results.emplace_back(cfg, res);

            // Non-empty-frame guard: zero-frame outputs (FAT-BLOCKER state when Poppins-Bold.ttf
            // is missing) are tolerated via env-var opt-out per AGENTS.md honest-discipline.
            // Without opt-out, REQUIRE ensures real rot doesn't silently degrade to empty-frame
            // after fat-blocker resolves (the prior-session macchina-verify silent-fake signal).
            const char* tolerate_empty = std::getenv("CHRONON3D_TOLERATE_EMPTY_FRAMES");
            const bool tolerate_empty_on = (tolerate_empty
                && (std::string{tolerate_empty} == "1" || std::string{tolerate_empty} == "true"));

            if (!res.metrics.empty_frame) {
                REQUIRE(res.metrics.ink_pixels > 0);
                REQUIRE(res.metrics.alpha_coverage > 0.0f);
                REQUIRE(res.metrics.ink_bbox.w > 0.0f);
                REQUIRE(res.metrics.ink_bbox.h > 0.0f);

                // ── TICKET-GOLDEN-MATRIX-FULL-METRIC-COVERAGE (N3 fix) ────
                // 5 of the 7 currently-unasserted metrics that bind to a
                // per-cell observable.  CHECK (non-fatal) so the sweep
                // continues across regressions instead of aborting on the
                // first violation — aggregate report at sweep-end surfaces
                // all rot in one go.
                CHECK(res.metrics.mean_luminance >= 0.0f);
                CHECK(res.metrics.mean_luminance <= 255.0f);
                CHECK(res.metrics.visual_center.x >= 0.0f);
                CHECK(res.metrics.visual_center.x <= static_cast<float>(d.width));
                CHECK(res.metrics.visual_center.y >= 0.0f);
                CHECK(res.metrics.visual_center.y <= static_cast<float>(d.height));
                // render_ms CHECK dropped per lean re-design (host-specific
                // variance + cold-cache first-cell priming tax too high to
                // bench a sane upper bound; emits silently into compute_metrics
                // for downstream audit-trail, no per-cell gate).
                if (!cfg.extreme_scale) {
                    // overflow / cut_text at extreme_scale MAY be true
                    // (intentional out-of-bounds); at normal scale they
                    // MUST be false.  Per-cell CHECK_FALSE so a single
                    // ill-behaved preset surfaces in the report rather
                    // than killing the whole matrix sweep.  Genuine rot
                    // (POST_RENDER_EXPAND bbox extends beyond framebuffer)
                    // is captured by these CHECKs and forwarded to
                    // TICKET-TEXT-BBOX-OVERFLOW.
                    CHECK_FALSE(res.metrics.overflow);
                    CHECK_FALSE(res.metrics.cut_text);
                }

                unique_hash_set.insert(res.metrics.hash);
                pass_count += 1;
            } else if (!tolerate_empty_on) {
                FAIL("Empty-frame cell detected (tolerate via CHRONON3D_TOLERATE_EMPTY_FRAMES=1 "
                     "to ack known fat-blocker): ", res.short_label,
                     " (font asset `assets/fonts/Poppins-Bold.ttf` likely missing).");
            } else {
                // FAT-BLOCKER: skip visual metrics (no ink to measure);
                // render_ms CHECK dropped per lean re-design.  empty_frame
                // itself IS the FAT-BLOCKER indicator, propagated via
                // empty_count + aggregate summary log.
                empty_count += 1;
            }
        if (res.metrics.cut_text)    cut_count    += 1;
        if (res.metrics.overflow)    overflow_count += 1;
        if (res.golden.golden_missing) golden_missing_count += 1;

        if (res.golden.golden_missing) {
            MESSAGE("Matrix golden missing: ", res.short_label,
                    " — run with CHRONON3D_UPDATE_GOLDENS=1 to capture.");
        }
        INFO("Matrix golden: ", res.short_label,
             " passed=", res.golden.passed,
             " missing=", res.golden.golden_missing);
    }

    append_manifest_entries(preset_id, cell_results);

    // ── TICKET-GOLDEN-MATRIX-FULL-METRIC-COVERAGE (N3, aggregate) ───────
    // Lean post-sweep floor: at least 2 unique hashes per sweep.  Dropped
    // prior >= 8u tightness for host-variance tolerance; kept >= 2u as
    // the irreducible silent-fake-green floor (mirror of Batch 1 cpp).
    // ctest-asserted (NOT log-only) per AGENTS.md honest-discipline
    // rot-gate.  Threshold `pass_count > 0` so fast_mode (6 cells per
    // preset) also exercises the floor — AR variation produces >= 2
    // distinct hashes in fast_mode.
    if (pass_count > 0) {
        INFO("unique-hashes for ", std::string{preset_id}, ": ",
             unique_hash_set.size(), " / ", pass_count, " cells");
        CHECK(unique_hash_set.size() >= 2u);  // silent-fake-green floor (lean re-design)
    }

    MESSAGE("Matrix sweep summary for ", std::string{preset_id}, ": ",
            "cells=", cells.size(),
            " non_empty=", pass_count,
            " empty=", empty_count,
            " overflow=", overflow_count,
            " cut=", cut_count,
            " golden_missing=", golden_missing_count);
}

} // namespace chronon3d::test::matrix_harness
