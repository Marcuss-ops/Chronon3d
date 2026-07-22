// SPDX-License-Identifier: MIT
//
// test_capcut_parity.cpp — CapCut-grade parity test harness.
//
// FU09 of TICKET-CAPCUT-REFERENCE-CORPUS.
//
// Loads tests/reference/capcut/manifest.json, renders each described case to
// tests/reference/capcut/<area>/current/<id>.png, and compares it against the
// blessed CapCut reference in tests/reference/capcut/<area>/reference/<id>.png.
//
// Metrics (verdict §Fase 9):
//   - baseline err  ≤ 1 px
//   - bbox err      ≤ 2 px per lato
//   - SSIM-on-ROI   ≥ 0.95
//   - changed_pixel_ratio (ROI) ≤ 5%
//   - silhouette missing-ratio ≤ 5%
//   - missing_glyphs == 0
//   - cut_text       == false
//
// Environment variables:
//   CHRONON3D_CAPCUT_RENDER_CURRENT=1  -> render/update current/ outputs even when
//                                       reference/ is empty.
//
// Cat-3 minimal-surface: metric helpers inlined here (not extracted to
// tests/visual/support/capcut_metrics.hpp yet). Forward-point: refactor to a
// header-only shared module IF another test reuses these helpers.

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/text_direction.hpp>

#include <nlohmann/json.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>
#include <tests/visual/support/image_diff.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── CapCut-grade parity thresholds (verdict §Fase 9) ─────────────────────
constexpr double kBaselineErrMaxPx       = 1.0;
constexpr double kBboxErrMaxPx           = 2.0;
constexpr double kSsimMinOnRoi           = 0.95;
constexpr double kChangedPixelRatioMaxRoi = 0.05;
constexpr double kSilhouetteMissingMax   = 0.05;
constexpr int    kMissingGlyphsMax        = 0;

// ── Subdir helpers ─────────────────────────────────────────────────────
const fs::path kCapcutRoot = "tests/reference/capcut";

bool render_current_env_enabled() {
    const char* v = std::getenv("CHRONON3D_CAPCUT_RENDER_CURRENT");
    return v != nullptr && (std::string_view{v} == "1" || std::string_view{v} == "true");
}

// ── Reference case model ───────────────────────────────────────────────
struct CapcutCase {
    std::string id;
    std::string area;
    std::string text;
    std::string font_path;
    std::string font_family;
    int         font_weight = 400;
    float       font_size   = 48.0f;
    float       tracking    = 0.0f;
    float       line_spacing = 1.2f;
    int         width       = 1920;
    int         height      = 1080;
    std::string placement   = "CanvasCenter";
    float       offset_x    = 0.0f;
    float       offset_y    = 0.0f;
    std::string align       = "Center";
    std::string vertical_align = "Middle";
    std::string effect      = "none";
    int         frame       = 0;
};

chronon3d::TextPlacementKind parse_placement(std::string_view s) {
    if (s == "Absolute") return chronon3d::TextPlacementKind::Absolute;
    if (s == "CanvasCenter") return chronon3d::TextPlacementKind::CanvasCenter;
    return chronon3d::TextPlacementKind::CanvasCenter;
}

chronon3d::TextAlign parse_align(std::string_view s) {
    if (s == "Left")   return chronon3d::TextAlign::Left;
    if (s == "Right")  return chronon3d::TextAlign::Right;
    return chronon3d::TextAlign::Center;
}

chronon3d::VerticalAlign parse_vertical_align(std::string_view s) {
    if (s == "Top")    return chronon3d::VerticalAlign::Top;
    if (s == "Bottom") return chronon3d::VerticalAlign::Bottom;
    return chronon3d::VerticalAlign::Middle;
}

std::vector<CapcutCase> load_manifest(const fs::path& repo_root) {
    std::vector<CapcutCase> cases;
    const fs::path manifest_path = repo_root / kCapcutRoot / "manifest.json";
    if (!fs::exists(manifest_path)) {
        INFO("manifest not found: " << manifest_path.string());
        return cases;
    }

    std::ifstream f(manifest_path);
    if (!f) return cases;
    nlohmann::json js;
    try {
        f >> js;
    } catch (const std::exception&) {
        return cases;
    }

    if (!js.contains("cases") || !js["cases"].is_array()) return cases;

    for (const auto& item : js["cases"]) {
        CapcutCase c;
        c.id            = item.value("id", std::string{});
        c.area          = item.value("area", std::string{});
        c.text          = item.value("text", std::string{});
        c.font_path     = item.value("font_path", std::string{});
        c.font_family   = item.value("font_family", std::string{});
        c.font_weight   = item.value("font_weight", 400);
        c.font_size     = item.value("font_size", 48.0f);
        c.tracking      = item.value("tracking", 0.0f);
        c.line_spacing  = item.value("line_spacing", 1.2f);
        c.width         = item.value("width", 1920);
        c.height        = item.value("height", 1080);
        c.placement     = item.value("placement", std::string{"CanvasCenter"});
        if (item.contains("offset") && item["offset"].is_array() && item["offset"].size() >= 2) {
            c.offset_x = item["offset"][0].get<float>();
            c.offset_y = item["offset"][1].get<float>();
        }
        c.align         = item.value("align", std::string{"Center"});
        c.vertical_align = item.value("vertical_align", std::string{"Middle"});
        c.effect        = item.value("effect", std::string{"none"});
        c.frame         = item.value("frame", 0);
        cases.push_back(std::move(c));
    }
    return cases;
}

// ── Rendering helpers ──────────────────────────────────────────────────

std::shared_ptr<chronon3d::Framebuffer> render_static_text_case(
    const CapcutCase& c,
    chronon3d::SoftwareRenderer& renderer)
{
    using namespace chronon3d;
    const auto comp = composition(
        {.name = c.id,
         .width = c.width,
         .height = c.height,
         .frame_rate = FrameRate{30, 1},
         .duration = std::max(1, c.frame + 1)},
        [&c, &renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer(c.id + "_layer", [&c, &renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                TextRunSpec spec;
                spec.text.content.value = c.text;
                spec.text.placement = TextPlacement{
                    parse_placement(c.placement),
                    Vec2{c.offset_x, c.offset_y}};
                spec.text.font.font_path   = chronon3d::test::bundled_font_path(c.font_path);
                spec.text.font.font_family = c.font_family;
                spec.text.font.font_weight = c.font_weight;
                spec.text.font.font_size   = c.font_size;
                spec.text.layout.box            = Vec2{static_cast<float>(c.width), static_cast<float>(c.height)};
                spec.text.layout.align          = parse_align(c.align);
                spec.text.layout.vertical_align = parse_vertical_align(c.vertical_align);
                spec.text.layout.line_height    = c.line_spacing;
                spec.text.layout.tracking       = c.tracking;
                spec.text.appearance.color = Color::white();
                spec.direction = TextDirection::LTR;
                spec.language  = "en";
                l.animated_text(c.id + "_text", std::move(spec)).commit();
            });
            return s.build();
        });
    return renderer.render(comp, Frame{c.frame});
}

std::shared_ptr<chronon3d::Framebuffer> render_subtitle_case(
    const CapcutCase& c,
    chronon3d::SoftwareRenderer& renderer)
{
    // Same rendering path as static; placement/alignment carry the
    // subtitle semantics (bottom-centered). Future karaoke word-timing
    // cases can switch to SubtitleTrackBuilder here.
    return render_static_text_case(c, renderer);
}

std::shared_ptr<chronon3d::Framebuffer> render_effect_case(
    const CapcutCase& c,
    chronon3d::SoftwareRenderer& renderer)
{
    using namespace chronon3d;
    if (c.effect == "opacity_fade") {
        const auto comp = composition(
            {.name = c.id,
             .width = c.width,
             .height = c.height,
             .frame_rate = FrameRate{30, 1},
             .duration = std::max(31, c.frame + 1)},
            [&c, &renderer](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx);
                s.font_engine(&renderer.font_engine());
                s.layer(c.id + "_layer", [&c, &renderer](LayerBuilder& l) {
                    l.font_engine(&renderer.font_engine());
                    l.opacity_anim().set(0.0f);
                    l.opacity_anim().add_keyframe(
                        Frame{30}, 1.0f, EasingCurve{Easing::Linear});
                    TextRunSpec spec;
                    spec.text.content.value = c.text;
                    spec.text.placement = TextPlacement{
                        parse_placement(c.placement),
                        Vec2{c.offset_x, c.offset_y}};
                    spec.text.font.font_path   = chronon3d::test::bundled_font_path(c.font_path);
                    spec.text.font.font_family = c.font_family;
                    spec.text.font.font_weight = c.font_weight;
                    spec.text.font.font_size   = c.font_size;
                    spec.text.layout.align          = parse_align(c.align);
                    spec.text.layout.vertical_align = parse_vertical_align(c.vertical_align);
                    spec.text.layout.line_height    = c.line_spacing;
                    spec.text.layout.tracking       = c.tracking;
                    spec.text.appearance.color = Color::white();
                    spec.direction = TextDirection::LTR;
                    spec.language  = "en";
                    l.animated_text(c.id + "_text", std::move(spec)).commit();
                });
                return s.build();
            });
        return renderer.render(comp, Frame{c.frame});
    }
    return render_static_text_case(c, renderer);
}

std::shared_ptr<chronon3d::Framebuffer> render_case(
    const CapcutCase& c,
    chronon3d::SoftwareRenderer& renderer)
{
    if (c.area == "static")    return render_static_text_case(c, renderer);
    if (c.area == "subtitles") return render_subtitle_case(c, renderer);
    if (c.area == "effects")   return render_effect_case(c, renderer);
    return render_static_text_case(c, renderer);
}

// ── Inline metric helpers (Cat-3 minimal-surface, refactor on reuse) ───

double compute_baseline_error(const chronon3d::Framebuffer& actual,
                              const chronon3d::Framebuffer& expected,
                              float alpha_epsilon = 0.01f) {
    using namespace chronon3d::test::completeness;
    const auto actual_extent   = ink_vertical_extent(actual,   alpha_epsilon);
    const auto expected_extent = ink_vertical_extent(expected, alpha_epsilon);
    if (actual_extent.first < 0 || expected_extent.first < 0) {
        return -1.0;
    }
    return std::abs(static_cast<double>(actual_extent.first - expected_extent.first));
}

struct BboxError {
    double left  = 0.0;
    double right = 0.0;
    double top   = 0.0;
    double bot   = 0.0;
};

BboxError compute_bbox_error(const chronon3d::Framebuffer& actual,
                             const chronon3d::Framebuffer& expected,
                             float alpha_epsilon = 0.01f) {
    using namespace chronon3d::test::completeness;
    const auto a = alpha_bbox(actual,   alpha_epsilon);
    const auto e = alpha_bbox(expected, alpha_epsilon);
    BboxError err;
    if (a.empty() || e.empty()) return err;
    err.left  = std::abs(static_cast<double>(a.x0 - e.x0));
    err.right = std::abs(static_cast<double>(a.x1 - e.x1));
    err.top   = std::abs(static_cast<double>(a.y0 - e.y0));
    err.bot   = std::abs(static_cast<double>(a.y1 - e.y1));
    return err;
}

int count_missing_glyphs(const chronon3d::Framebuffer& actual,
                         const chronon3d::Framebuffer& expected,
                         float alpha_epsilon = 0.01f) {
    if (actual.width() != expected.width() ||
        actual.height() != expected.height()) {
        return -1;
    }
    int missing = 0;
    for (int y = 0; y < expected.height(); ++y) {
        const chronon3d::Color* exp_row = expected.pixels_row(y);
        const chronon3d::Color* act_row = actual.pixels_row(y);
        for (int x = 0; x < expected.width(); ++x) {
            if (exp_row[x].a > alpha_epsilon && act_row[x].a <= 0.001f) {
                ++missing;
            }
        }
    }
    return missing;
}

bool detect_cut_text(const chronon3d::Framebuffer& actual,
                     float alpha_epsilon = 0.01f,
                     int edge_tolerance_px = 1) {
    using namespace chronon3d::test::completeness;
    const auto b = alpha_bbox(actual, alpha_epsilon);
    if (b.empty()) return false;
    return (b.x1 >= actual.width()  - 1 - edge_tolerance_px) ||
           (b.y1 >= actual.height() - 1 - edge_tolerance_px);
}

struct SilhouetteResult {
    double missing_ratio = 0.0;
    double mean_distance_px = 0.0;
};

/// Compute silhouette coverage error: for every expected-ink pixel, find the
/// nearest actual-ink pixel within max_distance_px. missing_ratio counts how
/// many expected silhouette pixels are not covered by the actual silhouette
/// within the dilation radius.
SilhouetteResult compute_silhouette_error(const chronon3d::Framebuffer& actual,
                                          const chronon3d::Framebuffer& expected,
                                          int max_distance_px = 2,
                                          float alpha_epsilon = 0.01f) {
    SilhouetteResult res;
    if (actual.width() != expected.width() ||
        actual.height() != expected.height()) {
        return res;
    }
    const int w = expected.width();
    const int h = expected.height();

    double total_distance = 0.0;
    int expected_count = 0;
    int missing_count = 0;

    for (int y = 0; y < h; ++y) {
        const chronon3d::Color* exp_row = expected.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            if (exp_row[x].a <= alpha_epsilon) continue;
            ++expected_count;
            bool found = false;
            int min_d = max_distance_px + 1;
            for (int dy = -max_distance_px; dy <= max_distance_px; ++dy) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                const chronon3d::Color* act_row = actual.pixels_row(yy);
                const int dx_max = max_distance_px - std::abs(dy);
                for (int dx = -dx_max; dx <= dx_max; ++dx) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= w) continue;
                    if (act_row[xx].a > alpha_epsilon) {
                        int d = std::abs(dx) + std::abs(dy);
                        if (d < min_d) min_d = d;
                        found = true;
                    }
                }
            }
            if (found) {
                total_distance += static_cast<double>(min_d);
            } else {
                ++missing_count;
            }
        }
    }

    if (expected_count > 0) {
        res.missing_ratio = static_cast<double>(missing_count) / static_cast<double>(expected_count);
        const int matched = expected_count - missing_count;
        res.mean_distance_px = matched > 0 ? total_distance / static_cast<double>(matched) : 0.0;
    }
    return res;
}

std::unique_ptr<chronon3d::Framebuffer> crop_framebuffer(
    const chronon3d::Framebuffer& src,
    int x0, int y0, int x1, int y1)
{
    using namespace chronon3d;
    const int w = x1 - x0 + 1;
    const int h = y1 - y0 + 1;
    auto out = std::make_unique<Framebuffer>(w, h);
    for (int y = 0; y < h; ++y) {
        int sy = y0 + y;
        const Color* row = src.pixels_row(sy);
        for (int x = 0; x < w; ++x) {
            out->set_pixel(x, y, row[x0 + x]);
        }
    }
    return out;
}

struct ParityReport {
    double baseline_err = 0.0;
    BboxError bbox_err{};
    double ssim = 1.0;
    double changed_pixel_ratio = 0.0;
    double silhouette_missing_ratio = 0.0;
    int missing_glyphs = 0;
    bool cut_text = false;
    bool ok = true;

    std::string to_string() const {
        std::ostringstream oss;
        oss << "baseline_err=" << baseline_err
            << " bbox[left=" << bbox_err.left << ",right=" << bbox_err.right
            << ",top=" << bbox_err.top << ",bot=" << bbox_err.bot << "]"
            << " ssim=" << ssim
            << " changed_px=" << changed_pixel_ratio
            << " silhouette_missing=" << silhouette_missing_ratio
            << " missing_glyphs=" << missing_glyphs
            << " cut_text=" << (cut_text ? "yes" : "no");
        return oss.str();
    }
};

ParityReport compare_against_reference(
    const chronon3d::Framebuffer& actual,
    const chronon3d::Framebuffer& expected)
{
    using namespace chronon3d::test::completeness;
    ParityReport report;

    report.baseline_err = compute_baseline_error(actual, expected);
    report.bbox_err     = compute_bbox_error(actual, expected);
    report.missing_glyphs = count_missing_glyphs(actual, expected);
    report.cut_text     = detect_cut_text(actual);

    const auto a_bbox = alpha_bbox(actual,   0.01f);
    const auto e_bbox = alpha_bbox(expected, 0.01f);
    if (!a_bbox.empty() && !e_bbox.empty()) {
        const int x0 = std::min(a_bbox.x0, e_bbox.x0);
        const int y0 = std::min(a_bbox.y0, e_bbox.y0);
        const int x1 = std::max(a_bbox.x1, e_bbox.x1);
        const int y1 = std::max(a_bbox.y1, e_bbox.y1);
        auto roi_actual   = crop_framebuffer(actual,   x0, y0, x1, y1);
        auto roi_expected = crop_framebuffer(expected, x0, y0, x1, y1);
        if (roi_actual && roi_expected) {
            report.ssim = chronon3d::test::compute_ssim(*roi_actual, *roi_expected);
            auto diff = chronon3d::test::compare_framebuffers(*roi_actual, *roi_expected);
            report.changed_pixel_ratio = diff.changed_pixel_ratio;
        }
    }

    auto silhouette = compute_silhouette_error(actual, expected, 2, 0.01f);
    report.silhouette_missing_ratio = silhouette.missing_ratio;

    report.ok = report.baseline_err <= kBaselineErrMaxPx &&
                report.bbox_err.left <= kBboxErrMaxPx &&
                report.bbox_err.right <= kBboxErrMaxPx &&
                report.bbox_err.top <= kBboxErrMaxPx &&
                report.bbox_err.bot <= kBboxErrMaxPx &&
                report.ssim >= kSsimMinOnRoi &&
                report.changed_pixel_ratio <= kChangedPixelRatioMaxRoi &&
                report.silhouette_missing_ratio <= kSilhouetteMissingMax &&
                report.missing_glyphs == kMissingGlyphsMax &&
                !report.cut_text;
    return report;
}

} // namespace

// ── TEST_CASEs (one per area: static / subtitles / effects) ───────────

TEST_CASE("capcut/static: ROI + baseline + bbox + silhouette + SSIM + missing_glyphs + cut_text") {
    const auto repo_root = chronon3d::test::test_repo_root();
    const auto all_cases = load_manifest(repo_root);
    const bool render_current = render_current_env_enabled();
    int compared = 0;
    int skipped_no_ref = 0;

    for (const auto& c : all_cases) {
        if (c.area != "static") continue;

        const fs::path current_path  = repo_root / kCapcutRoot / "static" / "current"  / (c.id + ".png");
        const fs::path reference_path = repo_root / kCapcutRoot / "static" / "reference" / (c.id + ".png");

        auto renderer = chronon3d::test::make_renderer_shared();
        auto actual = render_case(c, *renderer);
        REQUIRE(actual != nullptr);

        if (render_current) {
            fs::create_directories(current_path.parent_path());
            REQUIRE(chronon3d::save_png(*actual, current_path.string()));
            MESSAGE("rendered current/" << c.id << ".png");
        }

        if (!fs::exists(reference_path)) {
            ++skipped_no_ref;
            MESSAGE("missing blessed reference: " << reference_path.string());
            continue;
        }

        auto expected = chronon3d::test::load_png_as_framebuffer(reference_path.string());
        REQUIRE(expected != nullptr);

        const auto report = compare_against_reference(*actual, *expected);
        INFO(c.id << ": " << report.to_string());
        ++compared;
        CHECK(report.ok);
    }

    if (compared == 0) {
        MESSAGE("§blessed-reference static/ corpus is empty — see TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points (a)");
        MESSAGE("Set CHRONON3D_CAPCUT_RENDER_CURRENT=1 to generate current/ previews from the manifest.");
        CHECK(true);
    }

}

TEST_CASE("capcut/subtitles: ROI + baseline + bbox + silhouette + SSIM + missing_glyphs + cut_text") {
    const auto repo_root = chronon3d::test::test_repo_root();
    const auto all_cases = load_manifest(repo_root);
    const bool render_current = render_current_env_enabled();
    int compared = 0;
    int skipped_no_ref = 0;

    for (const auto& c : all_cases) {
        if (c.area != "subtitles") continue;

        const fs::path current_path  = repo_root / kCapcutRoot / "subtitles" / "current"  / (c.id + ".png");
        const fs::path reference_path = repo_root / kCapcutRoot / "subtitles" / "reference" / (c.id + ".png");

        auto renderer = chronon3d::test::make_renderer_shared();
        auto actual = render_case(c, *renderer);
        REQUIRE(actual != nullptr);

        if (render_current) {
            fs::create_directories(current_path.parent_path());
            REQUIRE(chronon3d::save_png(*actual, current_path.string()));
            MESSAGE("rendered current/" << c.id << ".png");
        }

        if (!fs::exists(reference_path)) {
            ++skipped_no_ref;
            MESSAGE("missing blessed reference: " << reference_path.string());
            continue;
        }

        auto expected = chronon3d::test::load_png_as_framebuffer(reference_path.string());
        REQUIRE(expected != nullptr);

        const auto report = compare_against_reference(*actual, *expected);
        INFO(c.id << ": " << report.to_string());
        ++compared;
        CHECK(report.ok);
    }

    if (compared == 0) {
        MESSAGE("§blessed-reference subtitles/ corpus is empty — see TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points (b)");
        MESSAGE("Set CHRONON3D_CAPCUT_RENDER_CURRENT=1 to generate current/ previews from the manifest.");
        CHECK(true);
    }
}

TEST_CASE("capcut/effects: ROI + baseline + bbox + silhouette + SSIM + missing_glyphs + cut_text") {
    const auto repo_root = chronon3d::test::test_repo_root();
    const auto all_cases = load_manifest(repo_root);
    const bool render_current = render_current_env_enabled();
    int compared = 0;
    int skipped_no_ref = 0;

    for (const auto& c : all_cases) {
        if (c.area != "effects") continue;

        const fs::path current_path  = repo_root / kCapcutRoot / "effects" / "current"  / (c.id + ".png");
        const fs::path reference_path = repo_root / kCapcutRoot / "effects" / "reference" / (c.id + ".png");

        auto renderer = chronon3d::test::make_renderer_shared();
        auto actual = render_case(c, *renderer);
        REQUIRE(actual != nullptr);

        if (render_current) {
            fs::create_directories(current_path.parent_path());
            REQUIRE(chronon3d::save_png(*actual, current_path.string()));
            MESSAGE("rendered current/" << c.id << ".png");
        }

        if (!fs::exists(reference_path)) {
            ++skipped_no_ref;
            MESSAGE("missing blessed reference: " << reference_path.string());
            continue;
        }

        auto expected = chronon3d::test::load_png_as_framebuffer(reference_path.string());
        REQUIRE(expected != nullptr);

        const auto report = compare_against_reference(*actual, *expected);
        INFO(c.id << ": " << report.to_string());
        ++compared;
        CHECK(report.ok);
    }

    if (compared == 0) {
        MESSAGE("§blessed-reference effects/ corpus is empty — see TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points (c)");
        MESSAGE("Set CHRONON3D_CAPCUT_RENDER_CURRENT=1 to generate current/ previews from the manifest.");
        CHECK(true);
    }
}

// ── Metric helper unit tests (Cat-3 invariant: helpers must be testable
//    independently of the blessed corpus). These always run. ───────────

TEST_CASE("capcut/metrics: compute_baseline_error detects identical frames (err=0)") {
    chronon3d::Framebuffer a(64, 64);
    chronon3d::Framebuffer e(64, 64);
    for (int x = 0; x < 64; ++x) {
        a.set_pixel(x, 10, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        e.set_pixel(x, 10, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
    }
    CHECK(compute_baseline_error(a, e) == doctest::Approx(0.0));
}

TEST_CASE("capcut/metrics: compute_bbox_error detects 2px shift") {
    chronon3d::Framebuffer a(64, 64);
    chronon3d::Framebuffer e(64, 64);
    for (int y = 10; y <= 20; ++y) {
        for (int x = 5; x <= 15; ++x) {
            a.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
            e.set_pixel(x + 2, y + 1, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    const auto err = compute_bbox_error(a, e);
    CHECK(err.left  == doctest::Approx(2.0));
    CHECK(err.top   == doctest::Approx(1.0));
    CHECK(err.right == doctest::Approx(2.0));
    CHECK(err.bot   == doctest::Approx(1.0));
}

TEST_CASE("capcut/metrics: detect_cut_text returns true when bbox touches right edge") {
    chronon3d::Framebuffer a(64, 64);
    for (int y = 10; y <= 20; ++y) {
        for (int x = 60; x <= 63; ++x) {
            a.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    CHECK(detect_cut_text(a) == true);
}

TEST_CASE("capcut/metrics: count_missing_glyphs == 0 when actual matches expected") {
    chronon3d::Framebuffer a(64, 64);
    chronon3d::Framebuffer e(64, 64);
    for (int y = 10; y <= 20; ++y) {
        for (int x = 10; x <= 20; ++x) {
            a.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
            e.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    CHECK(count_missing_glyphs(a, e) == 0);
}

TEST_CASE("capcut/metrics: silhouette missing-ratio is 0 for identical frames") {
    chronon3d::Framebuffer a(64, 64);
    chronon3d::Framebuffer e(64, 64);
    for (int y = 10; y <= 20; ++y) {
        for (int x = 10; x <= 20; ++x) {
            a.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
            e.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    auto s = compute_silhouette_error(a, e, 2, 0.01f);
    CHECK(s.missing_ratio == doctest::Approx(0.0));
}

TEST_CASE("capcut/metrics: silhouette detects a missing glyph") {
    chronon3d::Framebuffer a(64, 64);
    chronon3d::Framebuffer e(64, 64);
    for (int y = 10; y <= 20; ++y) {
        for (int x = 10; x <= 20; ++x) {
            e.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    // actual has no ink at all → every expected silhouette pixel is missing.
    auto s = compute_silhouette_error(a, e, 2, 0.01f);
    CHECK(s.missing_ratio == doctest::Approx(1.0));
}
