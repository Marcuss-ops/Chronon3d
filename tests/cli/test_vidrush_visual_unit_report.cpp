#include <doctest/doctest.h>

#include <chronon3d/vidrush/visual_unit_report.hpp>

#include <nlohmann/json.hpp>

namespace {

chronon3d::vidrush::VisualUnitReport make_report() {
    using namespace chronon3d::vidrush;
    VisualUnitReport report;
    report.scene_id = "scene-5";
    report.visual_unit_id = "scene-5.visual-2";
    report.time = {.start_ms = 10000, .end_ms = 25000, .target_duration_ms = 15000};
    report.semantic_profile.entities = {"Falcon 9", "SpaceX"};
    report.semantic_profile.important_phrases = {"booster landing"};
    report.semantic_profile.visual_terms = {"rocket landing"};
    report.provider_decision = {
        .selected = "youtube",
        .reason = "named entity and event footage require source video",
        .model = "small-model-id"};
    report.query_plan = {
        "SpaceX Falcon 9 booster landing",
        "Falcon 9 landing footage"};
    report.candidate = {
        .candidates_considered = 8,
        .selected_candidate = "youtube-video-42",
        .selected_source_duration_ms = 320000,
        .source_url = "https://www.youtube.com/watch?v=verified"};
    report.selected_window = {
        .start_ms = 184000,
        .end_ms = 199000,
        .duration_ms = 15000};
    report.asset = {
        .asset_id = "asset-42",
        .sha256 = "0123456789abcdef",
        .local_path = "assets/video/asset-42.mp4"};
    report.drive = {
        .destination = "vidrush/youtube/scene-5",
        .link = "https://drive.google.com/file/d/drive-42/view",
        .file_id = "drive-42"};
    report.cache = {
        .status = CacheStatus::Miss,
        .key = "sha256:0123456789abcdef",
        .discovery_calls = 1,
        .research_calls = 0,
        .provider_calls = 1,
        .download_calls = 1,
        .transcode_calls = 1,
        .drive_upload_calls = 1};
    return report;
}

}  // namespace

TEST_CASE("VidRush visual unit report serializes the sourcing contract") {
    using namespace chronon3d::vidrush;
    VisualUnitReportDocument document;
    document.job_id = "vidrush-golden-01";
    document.visual_units.push_back(make_report());

    const auto encoded = visual_unit_report_to_json(document);
    REQUIRE(encoded.has_value());
    const auto& json = encoded.value();

    CHECK(json.at("schema") == "vidrush.visual-unit-report.v1");
    CHECK(json.at("job_id") == "vidrush-golden-01");
    REQUIRE(json.at("visual_units").size() == 1);
    const auto& unit = json.at("visual_units").front();
    CHECK(unit.at("provider_decision").at("selected") == "youtube");
    CHECK(unit.at("query_plan").size() == 2);
    CHECK(unit.at("candidate").at("candidates_considered") == 8);
    CHECK(unit.at("selected_window").at("duration_ms") == 15000);
    CHECK(unit.at("asset").at("sha256") == "0123456789abcdef");
    CHECK(unit.at("drive").at("file_id") == "drive-42");
    CHECK(unit.at("cache").at("status") == "MISS");
}

TEST_CASE("VidRush visual unit report round-trips optional unavailable fields as null") {
    using namespace chronon3d::vidrush;
    VisualUnitReport report = make_report();
    report.provider_decision.selected = "artlist";
    report.candidate.selected_source_duration_ms.reset();
    report.candidate.source_url.reset();
    report.selected_window = {};
    report.drive.link.reset();
    report.drive.file_id.reset();
    report.cache.status = CacheStatus::Hit;
    report.cache.discovery_calls = 0;
    report.cache.research_calls = 0;
    report.cache.provider_calls = 0;
    report.cache.download_calls = 0;
    report.cache.transcode_calls = 0;
    report.cache.drive_upload_calls = 0;

    VisualUnitReportDocument document;
    document.job_id = "warm-run";
    document.visual_units = {report};
    const auto encoded = visual_unit_report_to_json(document);
    REQUIRE(encoded.has_value());
    const auto& unit = encoded.value().at("visual_units").front();
    CHECK(unit.at("candidate").at("selected_source_duration_ms").is_null());
    CHECK(unit.at("selected_window").at("start_ms").is_null());
    CHECK(unit.at("drive").at("link").is_null());
    CHECK(unit.at("cache").at("status") == "HIT");

    const auto decoded = visual_unit_report_from_json(encoded.value());
    REQUIRE(decoded.has_value());
    CHECK(decoded->visual_units.front().provider_decision.selected == "artlist");
    CHECK_FALSE(decoded->visual_units.front().candidate.source_url.has_value());
    CHECK(decoded->visual_units.front().cache.status == CacheStatus::Hit);
}

TEST_CASE("VidRush visual unit report rejects inconsistent timing") {
    using namespace chronon3d::vidrush;
    VisualUnitReport report = make_report();
    report.time.end_ms = 24000;
    VisualUnitReportDocument document;
    document.job_id = "invalid";
    document.visual_units = {report};

    const auto encoded = visual_unit_report_to_json(document);
    REQUIRE_FALSE(encoded.has_value());
    CHECK(encoded.error().path == "visual_units[0].target_duration_ms");
}

TEST_CASE("VidRush cache status vocabulary is stable") {
    using namespace chronon3d::vidrush;
    CHECK(std::string(cache_status_name(CacheStatus::Unknown)) == "UNKNOWN");
    CHECK(std::string(cache_status_name(CacheStatus::Hit)) == "HIT");
    CHECK(std::string(cache_status_name(CacheStatus::Miss)) == "MISS");
    CHECK(parse_cache_status("REBUILT") == CacheStatus::Rebuilt);
    CHECK_FALSE(parse_cache_status("invalid").has_value());
}
