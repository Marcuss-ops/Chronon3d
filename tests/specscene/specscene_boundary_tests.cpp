#include <doctest/doctest.h>
#include <chronon3d/specscene/specscene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/chronon3d.hpp>
#include <filesystem>
#include <fstream>

using namespace chronon3d;
using namespace chronon3d::specscene;

TEST_CASE("Test 14.1 — TOML compilation and pipeline rendering parity") {
    // 1. Create a temporary TOML specscene file
    const std::filesystem::path toml_path = "output/debug/temp_scene.toml";
    std::filesystem::create_directories(toml_path.parent_path());

    std::ofstream out(toml_path);
    out << R"(
[scene]
name = "SpecCompiledScene"
width = 100
height = 100
fps_numerator = 24
fps_denominator = 1
duration = 10

[[layer]]
id = "red_rect"
name = "red_rect"
position = [50.0, 50.0, 0.0]
scale = [1.0, 1.0, 1.0]
opacity = 1.0
is_3d = false

[[layer.visuals]]
type = "rect"
size = [40.0, 40.0]
color = [1.0, 0.0, 0.0, 1.0] # Red
pos = [0.0, 0.0, 0.0]
)";
    out.close();

    // Verify it is recognized as a specscene file
    CHECK(is_specscene_file(toml_path));

    // 2. Compile TOML file to Composition
    std::vector<std::string> diagnostics;
    auto opt_comp = compile_file(toml_path, &diagnostics);

    for (const auto& diag : diagnostics) {
        MESSAGE("TOML Loader Diagnostic: " << diag);
    }

    REQUIRE(opt_comp.has_value());
    Composition comp = std::move(*opt_comp);

    CHECK(comp.name() == "SpecCompiledScene");
    CHECK(comp.width() == 100);
    CHECK(comp.height() == 100);
    CHECK(comp.duration() == 10);

    // 3. Render frame 0 using modular graph engine
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // Verify that the red rect compiled and rendered in place
    // Center at (50, 50) is red
    CHECK(fb->get_pixel(50, 50).r > 0.9f);
    CHECK(fb->get_pixel(50, 50).g == 0.0f);
    CHECK(fb->get_pixel(50, 50).b == 0.0f);

    // Clean up temporary file
    std::filesystem::remove(toml_path);
}
