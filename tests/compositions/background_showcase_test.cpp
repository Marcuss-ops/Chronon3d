#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/core/composition_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <filesystem>
#include <fmt/format.h>

using namespace chronon3d;

#if 0
TEST_CASE("Render all frames of BackgroundShowcase and save as PNGs") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.tile_size = 256;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    CompositionRegistry registry;
    Composition comp = registry.create("BackgroundShowcase");
    REQUIRE(comp.name() == "BackgroundShowcase");
    REQUIRE(comp.duration() == 180);
    REQUIRE(comp.width() == 1920);
    REQUIRE(comp.height() == 1080);

    // Warmup
    {
        auto warmup_fb = renderer.render_frame(comp, 0);
        renderer.trace()->clear();
        renderer.counters()->reset();
    }

    std::filesystem::create_directories("output/background_showcase");

    // We render every 10th frame to keep execution fast but visually verify each segment
    for (int f = 0; f < 180; f += 10) {
        auto fb = renderer.render_frame(comp, f);
        REQUIRE(fb != nullptr);
        
        std::string filename = fmt::format("output/background_showcase/showcase.{:04d}.png", f);
        save_png(*fb, filename);
    }
}

TEST_CASE("Render individual 4s background showcases and compile to video") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.tile_size = 256;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    struct ShowcaseDef {
        std::string filename_prefix;
        std::string composition_name;
    };

    std::vector<ShowcaseDef> showcases = {
        {"studio_grid", "StudioGridShowcase"},
        {"gradient_orbs", "GradientOrbsShowcase"},
        {"parallax_space", "ParallaxSpaceShowcase"},
        {"data_stream", "DataStreamShowcase"},
        {"premium_studio", "PremiumStudioShowcase"}
    };

    std::filesystem::create_directories("output");

    CompositionRegistry registry;
    for (const auto& sc : showcases) {
        Composition comp = registry.create(sc.composition_name);
        std::string temp_dir = "output/temp_" + sc.filename_prefix;
        std::filesystem::create_directories(temp_dir);

        // Render all 120 frames (4 seconds at 30fps)
        for (int f = 0; f < 120; ++f) {
            auto fb = renderer.render_frame(comp, f);
            REQUIRE(fb != nullptr);
            std::string filepath = fmt::format("{}/frame.{:04d}.png", temp_dir, f);
            save_png(*fb, filepath);
        }

        // Run ffmpeg to compile the PNG sequence to an MP4 video (with faststart for browser streaming)
        std::string mp4_path = fmt::format("output/video_{}.mp4", sc.filename_prefix);
        std::string cmd = fmt::format("ffmpeg -y -framerate 30 -i {}/frame.%04d.png -c:v libx264 -pix_fmt yuv420p -crf 18 -movflags +faststart {} >/dev/null 2>&1", temp_dir, mp4_path);
        
        int ret = std::system(cmd.c_str());
        CHECK(ret == 0);

        // Clean up temporary frames directory
        std::filesystem::remove_all(temp_dir);
    }
}
#endif
