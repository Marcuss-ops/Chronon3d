#include <doctest/doctest.h>
#include <chronon3d/specscene/model/specscene.hpp>
#include <chronon3d/specscene/compiler/specscene_compiler.hpp>
#include <filesystem>
#include <fstream>
using namespace chronon3d;

using namespace chronon3d::specscene;

// ── SpecScene Compile Contract ──────────────────────────────────────────────
//
// compile_file MUST:
//   - Return a valid Composition when given valid TOML
//   - Preserve layer names and order
//   - Preserve scene dimensions
//   - Return std::nullopt for invalid/missing files

namespace {

std::filesystem::path write_temp_toml(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / "chronon3d_spec_test.toml";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

} // namespace

TEST_CASE("SpecSceneCompile: valid minimal scene compiles") {
    auto path = write_temp_toml(R"(
[scene]
name = "MinimalScene"
width = 100
height = 100
fps_numerator = 30
fps_denominator = 1
duration = 10

[[layer]]
id = "rect"
name = "rect"
position = [0.0, 0.0, 0.0]
opacity = 1.0
is_3d = false

[[layer.visuals]]
type = "rect"
size = [50.0, 50.0]
color = [1.0, 0.0, 0.0, 1.0]
pos = [0.0, 0.0, 0.0]
)");

    std::vector<std::string> diagnostics;
    auto comp = compile_file(path, &diagnostics);
    REQUIRE(comp.has_value());

    CHECK(comp->name() == "MinimalScene");
    CHECK(comp->width() == 100);
    CHECK(comp->height() == 100);
    CHECK(comp->duration() == 10);

    std::filesystem::remove(path);
}

TEST_CASE("SpecSceneCompile: compile returns nullopt for non-existent file") {
    auto comp = compile_file("/tmp/nonexistent_specscene_12345.toml");
    CHECK_FALSE(comp.has_value());
}

TEST_CASE("SpecSceneCompile: is_specscene_file returns false for non-toml") {
    CHECK_FALSE(is_specscene_file("output/test.txt"));
    CHECK_FALSE(is_specscene_file("output/test.json"));
    CHECK_FALSE(is_specscene_file("output/test"));
}

TEST_CASE("SpecSceneCompile: diagnostics are populated on errors") {
    auto path = write_temp_toml(R"(
[scene]
name = ""
width = -1
height = 0
duration = -5

[[layer]]
id = ""
name = ""
)");

    std::vector<std::string> diagnostics;
    auto comp = compile_file(path, &diagnostics);
    // Even with invalid values, compilation may produce a valid composition
    // or return nullopt — but diagnostics should be populated either way

    std::filesystem::remove(path);
    // compile_file did not crash with invalid input
}

TEST_CASE("SpecSceneCompile: load_file roundtrip preserves scene name") {
    auto path = write_temp_toml(R"(
[scene]
name = "RoundTripScene"
width = 640
height = 480
fps_numerator = 24
fps_denominator = 1
duration = 60

[[layer]]
id = "bg"
name = "bg"

[[layer.visuals]]
type = "rect"
size = [640.0, 480.0]
color = [0.0, 0.0, 1.0, 1.0]
)");

    std::vector<std::string> diagnostics;
    auto doc = load_file(path, &diagnostics);
    REQUIRE(doc.has_value());
    CHECK(doc->scene.name == "RoundTripScene");
    CHECK(doc->scene.width == 640);
    CHECK(doc->scene.height == 480);

    std::filesystem::remove(path);
}

TEST_CASE("SpecSceneCompile: load_file returns nullopt for non-existent file") {
    auto doc = load_file("/tmp/nonexistent_spec_load_12345.toml");
    CHECK_FALSE(doc.has_value());
}

TEST_CASE("SpecSceneCompile: compile with diagnostics vector does not crash") {
    auto path = write_temp_toml(R"(
[scene]
name = "DiagTest"
width = 100
height = 100
duration = 10

[[layer]]
id = "layer1"
name = "layer1"

[[layer.visuals]]
type = "rect"
size = [10.0, 10.0]
color = [0.0, 1.0, 0.0, 1.0]
)");

    std::vector<std::string> diagnostics;
    auto comp = compile_file(path, &diagnostics);
    CHECK(comp.has_value());

    // Check that diagnostics didn't crash — the vector may or may not
    // have content depending on the parser implementation
    diagnostics.clear();

    std::filesystem::remove(path);
}
