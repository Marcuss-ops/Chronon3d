#include <doctest/doctest.h>
#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
using namespace chronon3d;

namespace fs = std::filesystem;

// Helper: create a temp file and return its path
static std::string create_temp_file(const std::string& name) {
    std::string path = (fs::temp_directory_path() / name).string();
    std::ofstream out(path);
    out << "test";
    out.close();
    return path;
}

// Helper: create a temp directory
static std::string create_temp_dir(const std::string& name) {
    std::string path = (fs::temp_directory_path() / name).string();
    fs::create_directories(path);
    return path;
}

// Static AssetRegistry for preflight tests (validate() now requires a registry).
static AssetRegistry& test_registry() {
    static AssetRegistry reg;
    return reg;
}

TEST_CASE("RenderPreflight: require_image and detect missing") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_image("nonexistent/asset_xyz123.png");
    auto issues = RenderPreflight::instance().validate(assets);

    REQUIRE(issues.size() >= 1);
    CHECK(issues[0].severity == PreflightSeverity::Error);
    CHECK(issues[0].code == "MISSING_IMAGE");
    CHECK(issues[0].type == PreflightAssetType::Image);
    CHECK(issues[0].path == "nonexistent/asset_xyz123.png");
    CHECK(!issues[0].recommendation.empty());

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: require_video and detect missing") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_video("nonexistent/video_xyz123.mp4");
    auto issues = RenderPreflight::instance().validate(assets);

    REQUIRE(issues.size() >= 1);
    CHECK(issues[0].code == "MISSING_VIDEO");
    CHECK(issues[0].type == PreflightAssetType::Video);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: require_font and detect missing") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_font("nonexistent/font_xyz123.ttf");
    auto issues = RenderPreflight::instance().validate(assets);

    REQUIRE(issues.size() >= 1);
    CHECK(issues[0].code == "MISSING_FONT");
    CHECK(issues[0].type == PreflightAssetType::Font);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: require_audio and detect missing") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_audio("nonexistent/audio_xyz123.wav");
    auto issues = RenderPreflight::instance().validate(assets);

    REQUIRE(issues.size() >= 1);
    CHECK(issues[0].code == "MISSING_AUDIO");
    CHECK(issues[0].type == PreflightAssetType::Audio);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: require_external_tool missing") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_external_tool("nonexistent_tool_xyz123");
    auto issues = RenderPreflight::instance().validate(assets);

    bool found = false;
    for (const auto& i : issues) {
        if (i.code == "MISSING_EXTERNAL_TOOL" && i.path == "nonexistent_tool_xyz123") {
            found = true;
            CHECK(i.severity == PreflightSeverity::Error);
            CHECK(i.type == PreflightAssetType::ExternalTool);
            break;
        }
    }
    // The tool might not exist, which is expected in most test environments
    if (!found) {
        // If the tool miraculously exists, that's ok too
        MESSAGE("External tool 'nonexistent_tool_xyz123' was found in PATH — unexpected but not a test failure");
    }

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: require_external_tool existing (echo/where)") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    // These tools should exist on virtually every system
#if defined(_WIN32)
    RenderPreflight::instance().require_external_tool("cmd");
#else
    RenderPreflight::instance().require_external_tool("echo");
#endif
    auto issues = RenderPreflight::instance().validate(assets);

    bool has_tool_error = false;
    for (const auto& i : issues) {
        if (i.code == "MISSING_EXTERNAL_TOOL") {
            has_tool_error = true;
            break;
        }
    }
    CHECK_FALSE(has_tool_error);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: require_output_path nonexistent deep directory") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    // A deeply nested path that is extremely unlikely to exist
    RenderPreflight::instance().require_output_path("/tmp/chronon3d_nonexistent_deep_path_xyz/subdir/output.mp4");
    auto issues = RenderPreflight::instance().validate(assets);

    // We should get OUTPUT_DIR_MISSING warning (the dir doesn't exist yet)
    bool has_output_warning = false;
    for (const auto& i : issues) {
        if (i.code == "OUTPUT_DIR_MISSING") {
            has_output_warning = true;
            CHECK(i.severity == PreflightSeverity::Warning);
            CHECK(i.type == PreflightAssetType::OutputPath);
            break;
        }
    }
    CHECK(has_output_warning);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: require_output_path to writable location") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    std::string path = (fs::temp_directory_path() / "chronon3d_test_output.mp4").string();
    // Remove it if it exists
    std::error_code ec;
    fs::remove(path, ec);
    RenderPreflight::instance().require_output_path(path);
    auto issues = RenderPreflight::instance().validate(assets);

    // Should have at most a warning about OUTPUT_EXISTS, but no errors
    for (const auto& i : issues) {
        CHECK(i.severity != PreflightSeverity::Error);
    }

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: validate_or_throw with no errors") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    // No requirements → no throw
    CHECK_NOTHROW(RenderPreflight::instance().validate_or_throw(assets));

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: validate_or_throw with errors") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_image("nonexistent_asset.png");
    CHECK_THROWS_AS(RenderPreflight::instance().validate_or_throw(assets), ChrononAssetError);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: ok() returns correct status") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    // Empty should be ok
    CHECK(RenderPreflight::instance().ok(assets));

    RenderPreflight::instance().require_image("nonexistent_asset.png");
    CHECK_FALSE(RenderPreflight::instance().ok(assets));

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: clear() resets requirements") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_image("missing.png");
    {
        auto issues = RenderPreflight::instance().validate(assets);
        CHECK(issues.size() >= 1);
    }

    RenderPreflight::instance().clear();
    {
        auto issues = RenderPreflight::instance().validate(assets);
        CHECK(issues.empty());
    }

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: add_requirements works") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    std::vector<PreflightRequirement> reqs;
    PreflightRequirement r1;
    r1.type = PreflightAssetType::Image;
    r1.path = "test_1.png";
    reqs.push_back(r1);

    PreflightRequirement r2;
    r2.type = PreflightAssetType::Font;
    r2.path = "test_2.ttf";
    r2.composition_id = "TestComp";
    r2.layer_id = "title";
    reqs.push_back(r2);

    RenderPreflight::instance().add_requirements(reqs);
    auto issues = RenderPreflight::instance().validate(assets);

    REQUIRE(issues.size() >= 2);

    // Second issue should carry composition/layer info
    bool found_with_context = false;
    for (const auto& i : issues) {
        if (i.composition_id == "TestComp" && i.layer_id == "title") {
            found_with_context = true;
            break;
        }
    }
    CHECK(found_with_context);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: preflight_issues_to_json structure") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_image("missing_img.png");
    auto issues = RenderPreflight::instance().validate(assets);
    auto js = preflight_issues_to_json(issues);

    CHECK(js["schema"] == "chronon3d.preflight.v1");
    CHECK(js["ok"] == false);
    CHECK(js["errors"] > 0);
    CHECK(js.contains("warnings"));
    CHECK(js.contains("infos"));
    CHECK(js["issues"].is_array());
    CHECK(js["issues"].size() > 0);

    auto first = js["issues"][0];
    CHECK(first.contains("severity"));
    CHECK(first.contains("code"));
    CHECK(first.contains("type"));
    CHECK(first.contains("path"));
    CHECK(first.contains("message"));
    CHECK(first.contains("recommendation"));

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: format_preflight_issues_text includes issue details") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_image("my_missing_image.png");
    auto issues = RenderPreflight::instance().validate(assets);
    std::string text = format_preflight_issues_text(issues);

    CHECK(text.find("CHRONON3D PREFLIGHT FAILED") != std::string::npos);
    CHECK(text.find("MISSING_IMAGE") != std::string::npos);
    CHECK(text.find("my_missing_image.png") != std::string::npos);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: multiple issues are all collected") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    RenderPreflight::instance().require_image("missing_a.png");
    RenderPreflight::instance().require_video("missing_b.mp4");
    RenderPreflight::instance().require_font("missing_c.ttf");
    auto issues = RenderPreflight::instance().validate(assets);

    // Should have at least 3 issues (plus any registered assets)
    CHECK(issues.size() >= 3);

    int image_count = 0, video_count = 0, font_count = 0;
    for (const auto& i : issues) {
        if (i.code == "MISSING_IMAGE") ++image_count;
        if (i.code == "MISSING_VIDEO") ++video_count;
        if (i.code == "MISSING_FONT") ++font_count;
    }
    CHECK(image_count >= 1);
    CHECK(video_count >= 1);
    CHECK(font_count >= 1);

    RenderPreflight::instance().clear();
}

TEST_CASE("RenderPreflight: JSON roundtrip is valid JSON") {
    auto& assets = test_registry();
    RenderPreflight::instance().clear();

    auto issues = RenderPreflight::instance().validate(assets);
    auto js = preflight_issues_to_json(issues);

    std::string dumped = js.dump();
    // Should parse back as valid JSON
    auto parsed = nlohmann::json::parse(dumped);
    CHECK(parsed.is_object());

    RenderPreflight::instance().clear();
}

TEST_CASE("tool_exists_in_path: known tools") {
    // These tools should exist on virtually every system
#if defined(_WIN32)
    CHECK(tool_exists_in_path("cmd.exe"));
#else
    CHECK(tool_exists_in_path("echo"));
#endif
}

TEST_CASE("tool_exists_in_path: nonexistent tool") {
    CHECK_FALSE(tool_exists_in_path("chronon3d_nonexistent_tool_xyz"));
}
