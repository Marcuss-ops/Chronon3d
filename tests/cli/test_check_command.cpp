#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include "commands/render/check_audio.hpp"
#include "commands/render/check_findings.hpp"
#include "commands/render/check_plan.hpp"
#include "commands/render/check_video.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace chronon3d::cli::check;

namespace {

FrameMetrics make_frame(const int index, const double luma,
                        const double opaque = 1.0) {
    FrameMetrics frame;
    frame.frame = index;
    frame.mean_luma = luma;
    frame.visible_fraction = opaque;
    frame.opaque_fraction = opaque;
    frame.black = opaque >= 0.5 && luma <= 0.002;
    return frame;
}

VideoScan make_scan(const std::vector<FrameMetrics>& frames,
                    const double fps = 30.0) {
    VideoScan scan;
    scan.input = "synthetic";
    scan.is_video = true;
    scan.fps = fps;
    scan.sample_step = 1;
    scan.total_frames = static_cast<int>(frames.size());
    scan.sampled_frames = static_cast<int>(frames.size());
    scan.frames = frames;
    return scan;
}

bool has_finding(const std::vector<Finding>& findings, const std::string& id) {
    return std::any_of(findings.begin(), findings.end(),
        [&id](const Finding& finding) { return finding.id == id; });
}

const Finding* find_finding(const std::vector<Finding>& findings,
                            const std::string& id) {
    const auto it = std::find_if(findings.begin(), findings.end(),
        [&id](const Finding& finding) { return finding.id == id; });
    return it == findings.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("check severity: ordering, names and parsing agree") {
    CHECK(severity_at_least(Severity::P0, Severity::P1));
    CHECK(severity_at_least(Severity::P1, Severity::P1));
    CHECK_FALSE(severity_at_least(Severity::P2, Severity::P1));
    CHECK_FALSE(severity_at_least(Severity::P3, Severity::P2));

    CHECK(severity_name(Severity::P0) == "P0");
    CHECK(severity_name(Severity::P1) == "P1");
    CHECK(severity_name(Severity::P2) == "P2");
    CHECK(severity_name(Severity::P3) == "P3");

    CHECK(parse_severity("P0") == Severity::P0);
    CHECK(parse_severity("p1") == Severity::P1);
    CHECK(parse_severity("P2") == Severity::P2);
    CHECK(parse_severity("P3") == Severity::P3);
    CHECK_FALSE(parse_severity("high").has_value());
    CHECK_FALSE(parse_severity("").has_value());
}

TEST_CASE("check report: exit code and status honour fail_on") {
    Report report;
    report.input = "clip.mp4";
    report.thresholds.fail_on = Severity::P1;

    Finding p2;
    p2.id = "video.highlights_clipped";
    p2.category = "video";
    p2.severity = Severity::P2;
    p2.message = "clipped";
    add_finding(report, p2);

    CHECK(exit_code(report) == 0);
    CHECK_FALSE(has_failure(report));

    auto json = report_to_json(report);
    CHECK(json.at("schema") == "chronon.check.v1");
    CHECK(json.at("status") == "PASS");
    CHECK(json.at("summary").at("counts").at("P2") == 1);
    CHECK(json.at("summary").at("total") == 1);
    CHECK(json.at("findings").size() == 1);
    CHECK(json.at("findings")[0].at("severity") == "P2");

    Finding p1;
    p1.id = "audio.silent";
    p1.category = "audio";
    p1.severity = Severity::P1;
    p1.message = "silent";
    add_finding(report, p1);

    CHECK(has_failure(report));
    CHECK(exit_code(report) == 1);
    CHECK(severity_counts(report) == std::array<int, 4>{0, 1, 1, 0});

    report.thresholds.fail_on = Severity::P0;
    CHECK_FALSE(has_failure(report));
    CHECK(exit_code(report) == 0);
}

TEST_CASE("check report: duplicate check names are recorded once") {
    Report report;
    add_check_run(report, "video");
    add_check_run(report, "video");
    add_check_not_measured(report, "audio: no audio stream");
    add_check_not_measured(report, "audio: no audio stream");
    CHECK(report.checks_run.size() == 1);
    CHECK(report.checks_not_measured.size() == 1);

    const auto text = report_to_text(report);
    CHECK(text.find("checks: video") != std::string::npos);
    CHECK(text.find("not measured: audio: no audio stream") != std::string::npos);
}

TEST_CASE("check video: an all-black clip is a P0, a partial black run is P1") {
    Thresholds thresholds;
    thresholds.black_seconds = 0.5;

    std::vector<FrameMetrics> all_black;
    for (int index = 0; index < 45; ++index) {
        all_black.push_back(make_frame(index, 0.0));
    }
    const auto black_findings = analyze_video(make_scan(all_black), thresholds);
    const auto* black = find_finding(black_findings, "video.black");
    REQUIRE(black != nullptr);
    CHECK(black->severity == Severity::P0);
    CHECK(black->first_frame == 0);
    CHECK(black->last_frame == 44);
    CHECK_FALSE(has_finding(black_findings, "video.empty_alpha"));

    std::vector<FrameMetrics> partial;
    for (int index = 0; index < 30; ++index) {
        partial.push_back(make_frame(index, 0.0));
    }
    for (int index = 30; index < 60; ++index) {
        partial.push_back(make_frame(index, 0.5));
    }
    const auto partial_findings = analyze_video(make_scan(partial), thresholds);
    const auto* run = find_finding(partial_findings, "video.black");
    REQUIRE(run != nullptr);
    CHECK(run->severity == Severity::P1);
    CHECK(run->first_frame == 0);
    CHECK(run->last_frame == 29);
    CHECK(run->first_second.has_value());
    CHECK(*run->first_second == doctest::Approx(0.0));
    CHECK(*run->last_second == doctest::Approx(1.0));
}

TEST_CASE("check frame metrics: a dark linear background is not black") {
    chronon3d::Framebuffer framebuffer(4, 4);
    framebuffer.clear(chronon3d::Color{0.00075f, 0.00075f, 0.00075f, 1.0f});

    const auto metrics = measure_frame(framebuffer, 0, nullptr);
    CHECK_FALSE(metrics.black);
    CHECK(metrics.visible_fraction == doctest::Approx(1.0));
    CHECK(metrics.opaque_fraction == doctest::Approx(1.0));
    // 0.00075 linear is above the 1e-4 linear black floor.
    CHECK(metrics.near_black_fraction == doctest::Approx(0.0));
}

TEST_CASE("check frame metrics: a visible pixel keeps a black frame alive") {
    chronon3d::Framebuffer framebuffer(4, 4);
    framebuffer.clear(chronon3d::Color::black());
    framebuffer.set_pixel(0, 0, chronon3d::Color::white());

    const auto metrics = measure_frame(framebuffer, 0, nullptr);
    CHECK_FALSE(metrics.black);
    CHECK(metrics.near_black_fraction == doctest::Approx(15.0 / 16.0));
}

TEST_CASE("check frame metrics: transparent and translucent overlays use straight alpha") {
    chronon3d::Framebuffer transparent(2, 2);
    transparent.clear(chronon3d::Color::transparent());
    const auto empty = measure_frame(transparent, 0, nullptr);
    CHECK(empty.visible_fraction == doctest::Approx(0.0));
    CHECK_FALSE(empty.black);

    chronon3d::Framebuffer translucent(2, 2);
    translucent.clear(
        chronon3d::Color{1.0f, 1.0f, 1.0f, 0.5f}.premultiplied());
    const auto half = measure_frame(translucent, 0, nullptr);
    CHECK(half.visible_fraction == doctest::Approx(1.0));
    CHECK(half.opaque_fraction == doctest::Approx(0.0));
    // Premultiplied storage must not dim the measured straight-alpha luma.
    CHECK(half.mean_luma == doctest::Approx(1.0));
    CHECK_FALSE(half.black);
}

TEST_CASE("check video: a small visible overlay is not empty alpha") {
    auto frame = make_frame(0, 0.5);
    frame.opaque_fraction = 0.0;
    frame.visible_fraction = 0.0001;
    const auto findings = analyze_video(make_scan({frame}), Thresholds{});
    CHECK_FALSE(has_finding(findings, "video.empty_alpha"));
}

TEST_CASE("check video: transparent output is a P0") {
    std::vector<FrameMetrics> frames;
    for (int index = 0; index < 8; ++index) {
        frames.push_back(make_frame(index, 0.0, 0.0));
    }
    const auto findings = analyze_video(make_scan(frames), Thresholds{});
    const auto* empty = find_finding(findings, "video.empty_alpha");
    REQUIRE(empty != nullptr);
    CHECK(empty->severity == Severity::P0);
}

TEST_CASE("check video: a frozen hold becomes P1 after the floor") {
    Thresholds thresholds;
    thresholds.freeze_seconds = 1.0;
    std::vector<FrameMetrics> frames;
    for (int index = 0; index < 61; ++index) {
        auto frame = make_frame(index, 0.5);
        frame.diff_from_previous = index == 0 ? 0.0 : 0.0;
        frames.push_back(frame);
    }
    const auto findings = analyze_video(make_scan(frames), thresholds);
    const auto* freeze = find_finding(findings, "video.freeze");
    REQUIRE(freeze != nullptr);
    CHECK(freeze->severity == Severity::P1);
    CHECK(freeze->first_frame == 0);
    CHECK(freeze->last_frame == 60);
}

TEST_CASE("check video: alternating luminance trips the flash floor") {
    Thresholds thresholds;
    thresholds.flash_max_per_second = 3;
    std::vector<FrameMetrics> frames;
    for (int index = 0; index < 61; ++index) {
        frames.push_back(make_frame(index, index % 2 == 0 ? 0.1 : 0.9));
    }
    const auto findings = analyze_video(make_scan(frames), thresholds);
    const auto* flash = find_finding(findings, "video.flash");
    REQUIRE(flash != nullptr);
    CHECK(flash->severity == Severity::P1);
    CHECK(flash->details.at("transitions_per_second").get<int>() > 3);
}

TEST_CASE("check video: clipped highlights are P2 with the worst frame") {
    Thresholds thresholds;
    thresholds.max_clipped_fraction = 0.02;
    std::vector<FrameMetrics> frames;
    for (int index = 0; index < 4; ++index) {
        auto frame = make_frame(index, 0.5);
        if (index == 2) frame.clipped_high_fraction = 0.10;
        frames.push_back(frame);
    }
    const auto findings = analyze_video(make_scan(frames), thresholds);
    const auto* clipped = find_finding(findings, "video.highlights_clipped");
    REQUIRE(clipped != nullptr);
    CHECK(clipped->severity == Severity::P2);
    CHECK(clipped->first_frame == 2);
}

TEST_CASE("check plan: unresolved/small/empty text produces the expected IDs") {
    PlanFacts facts;
    facts.canvas_width = 1920;
    facts.canvas_height = 1080;

    PlanLayerFacts small;
    small.id = "title";
    small.type = "text";
    small.text = "Hello";
    small.font_resolved = false;
    small.font_size = 12.0f;
    facts.layers.push_back(small);

    PlanLayerFacts blank;
    blank.id = "subtitle";
    blank.type = "text";
    blank.text = "   ";
    blank.font_resolved = true;
    blank.font_size = 64.0f;
    blank.box_width = 0.0f;
    blank.safe_insets = std::array<float, 4>{-1.0f, 0.0f, 0.0f, 0.0f};
    facts.layers.push_back(blank);

    const auto findings = analyze_plan(facts, Thresholds{});
    CHECK(has_finding(findings, "text.font_unresolved"));
    CHECK(has_finding(findings, "text.too_small"));
    CHECK(has_finding(findings, "text.empty"));
    CHECK(has_finding(findings, "text.box_invalid"));
    CHECK(has_finding(findings, "plan.safe_insets_invalid"));

    const auto* unresolved = find_finding(findings, "text.font_unresolved");
    REQUIRE(unresolved != nullptr);
    CHECK(unresolved->severity == Severity::P0);

    const auto* too_small = find_finding(findings, "text.too_small");
    REQUIRE(too_small != nullptr);
    CHECK(too_small->severity == Severity::P1);
}

TEST_CASE("check plan: font floor scales with canvas height") {
    PlanFacts facts;
    facts.canvas_width = 3840;
    facts.canvas_height = 2160;

    PlanLayerFacts layer;
    layer.id = "title";
    layer.type = "text";
    layer.text = "Hello";
    layer.font_resolved = true;
    layer.font_size = 30.0f;
    facts.layers.push_back(layer);

    Thresholds thresholds;
    thresholds.min_font_px = 20.0;
    thresholds.min_font_reference_height = 1080.0;
    // At 2160p the floor doubles to 40 px, so 30 px is too small.
    const auto findings = analyze_plan(facts, thresholds);
    CHECK(has_finding(findings, "text.too_small"));

    facts.canvas_height = 1080;
    const auto hd_findings = analyze_plan(facts, thresholds);
    CHECK_FALSE(has_finding(hd_findings, "text.too_small"));
}

TEST_CASE("check audio: silence and too-short signals are -infinity LUFS") {
    const std::vector<float> silence(48000, 0.0f);
    const double loudness = integrated_loudness_lufs(silence, 48000);
    CHECK(std::isinf(loudness));
    CHECK(loudness < 0.0);
    CHECK(std::isinf(sample_peak_dbfs(silence)));
    CHECK(sample_peak_dbfs(silence) < 0.0);

    const std::vector<float> short_signal(100, 0.1f);
    CHECK(std::isinf(integrated_loudness_lufs(short_signal, 48000)));

    CHECK(std::isinf(sample_peak_dbfs({})));
}

TEST_CASE("check audio: a 1 kHz sine measures a finite integrated loudness") {
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kSampleRate = 48000;
    std::vector<float> samples(kSampleRate * 2);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = static_cast<float>(
            0.1 * std::sin(2.0 * kPi * 1000.0 *
                           static_cast<double>(index) / kSampleRate));
    }

    const double loudness = integrated_loudness_lufs(samples, kSampleRate);
    REQUIRE(std::isfinite(loudness));
    CHECK(loudness < -15.0);
    CHECK(loudness > -30.0);

    const double peak = sample_peak_dbfs(samples);
    REQUIRE(std::isfinite(peak));
    CHECK(peak == doctest::Approx(20.0 * std::log10(0.1)).epsilon(0.001));
}

TEST_CASE("check audio: sample peak is not true peak") {
    const double peak = sample_peak_dbfs({0.5f, -0.25f, 0.0f});
    CHECK(peak == doctest::Approx(20.0 * std::log10(0.5)).epsilon(0.0001));
}

TEST_CASE("check audio: loudness, silence and peak findings map to thresholds") {
    Thresholds thresholds;
    AudioMetrics metrics;
    metrics.has_audio_stream = true;
    metrics.measured = true;
    metrics.integrated_lufs = -20.0;
    metrics.peak_dbfs = -3.0;// under the ceiling

    auto findings = analyze_audio_metrics(metrics, thresholds);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].id == "audio.loudness");
    CHECK(findings[0].severity == Severity::P1);

    metrics.integrated_lufs = -14.0;
    metrics.peak_dbfs = 0.0;// over the -1 dBFS ceiling
    findings = analyze_audio_metrics(metrics, thresholds);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].id == "audio.peak");

    metrics.integrated_lufs = -120.0;
    metrics.peak_dbfs = -120.0;
    findings = analyze_audio_metrics(metrics, thresholds);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].id == "audio.silent");
    CHECK(findings[0].severity == Severity::P1);
}

TEST_CASE("check audio: an unmeasurable stream is reported, never assumed clean") {
    AudioMetrics metrics;
    metrics.has_audio_stream = true;
    metrics.measured = false;
    metrics.error = "audio decode failed";

    const auto findings = analyze_audio_metrics(metrics, Thresholds{});
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].id == "audio.not_measured");
    CHECK(findings[0].severity == Severity::P2);

    AudioMetrics no_stream;
    CHECK(analyze_audio_metrics(no_stream, Thresholds{}).empty());
}

TEST_CASE("check scan: missing/unreadable inputs return errors, not exceptions") {
    const auto plan = scan_plan("/definitely/not/a/render_plan.json", "");
    CHECK_FALSE(plan.facts.has_value());
    CHECK_FALSE(plan.error.empty());

    const auto unsupported = scan_video("/tmp/not_a_video.unknown", Thresholds{}, false);
    CHECK_FALSE(unsupported.scan.has_value());
    CHECK_FALSE(unsupported.error.empty());

    const auto missing_image = scan_video("/tmp/chronon_check_missing.png",
                                          Thresholds{}, false);
    CHECK_FALSE(missing_image.scan.has_value());
    CHECK_FALSE(missing_image.error.empty());
}
