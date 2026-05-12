#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/loader.hpp>
#include <chronon3d/core/pipeline.hpp>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>
#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <iostream>
#include <filesystem>

using namespace chronon3d;

ABSL_FLAG(int64_t, frame, -1, "Single frame to render (overrides --start and --end)");
ABSL_FLAG(int64_t, start, 0, "Start frame of the range");
ABSL_FLAG(int64_t, end, 1, "End frame of the range (exclusive)");
ABSL_FLAG(std::string, output, "render.ppm", "Output path for the rendered frames");

int main(int argc, char** argv) {
    auto positional_args = absl::ParseCommandLine(argc, argv);
    
    ZoneScopedN("Main");
    spdlog::info("Chronon3d CLI v0.1.0");

    if (positional_args.size() < 2) {
        std::cout << "Usage: chronon3d_cli <scene.json> [flags]" << std::endl;
        std::cout << "Flags:" << std::endl;
        std::cout << "  --frame N    Single frame to render" << std::endl;
        std::cout << "  --start S    Start frame" << std::endl;
        std::cout << "  --end E      End frame" << std::endl;
        std::cout << "  --output P   Output path" << std::endl;
        return 1;
    }

    try {
        std::string scene_path = positional_args[1];
        auto comp = SceneLoader::load_from_file(scene_path);
        spdlog::info("Loaded composition: {} ({}x{})", comp->name(), comp->width(), comp->height());

        i64 start_frame = absl::GetFlag(FLAGS_start);
        i64 end_frame = absl::GetFlag(FLAGS_end);
        bool single_frame = false;
        std::string output_path = absl::GetFlag(FLAGS_output);

        if (absl::GetFlag(FLAGS_frame) != -1) {
            start_frame = absl::GetFlag(FLAGS_frame);
            end_frame = start_frame + 1;
            single_frame = true;
        } else if (end_frame <= start_frame) {
            // Default behavior if end is not set or invalid
            end_frame = start_frame + 1;
            single_frame = true;
        }

        if (single_frame && end_frame == 0) end_frame = comp->duration();

        auto renderer = std::make_shared<SoftwareRenderer>();
        RenderPipeline pipeline(comp, renderer);

        spdlog::info("Rendering frames {} to {}...", start_frame, end_frame);
        
        pipeline.run(start_frame, end_frame, [&](RenderedFrame rf) {
            std::string path = output_path;
            if (!single_frame) {
                std::filesystem::path p(output_path);
                path = (p.parent_path() / fmt::format("{}_{:04d}{}", p.stem().string(), rf.frame, p.extension().string())).string();
            }
            spdlog::info("Saving frame {} to {}...", rf.frame, path);
            rf.framebuffer->save_ppm(path);
            
            FrameMark; // Tracy frame mark
        });

        spdlog::info("Done!");

    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return 1;
    }

    return 0;
}
