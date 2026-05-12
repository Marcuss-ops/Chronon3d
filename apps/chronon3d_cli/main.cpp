#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/pipeline.hpp>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>
#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <iostream>
#include <filesystem>

using namespace chronon3d;

ABSL_FLAG(std::string, composition, "", "ID of the composition to render");
ABSL_FLAG(int64_t, frame, -1, "Single frame to render (overrides --start and --end)");
ABSL_FLAG(int64_t, start, 0, "Start frame of the range");
ABSL_FLAG(int64_t, end, -1, "End frame of the range (exclusive)");
ABSL_FLAG(std::string, output, "render.ppm", "Output path for the rendered frames");

// CLI Entry point

int main(int argc, char** argv) {
    auto positional_args = absl::ParseCommandLine(argc, argv);
    
    ZoneScopedN("Main");
    spdlog::info("Chronon3d CLI v0.1.0 (Code-First)");

    // Register examples if not already done
    CompositionRegistry::instance().add("CodeFirstSmoke", []() {
        CompositionSpec spec;
        spec.name = "CodeFirstSmoke";
        spec.width = 512;
        spec.height = 512;
        spec.frame_rate = {30, 1};
        spec.duration = 60;

        return Composition{
            spec,
            [](const FrameContext& ctx) {
                SceneBuilder builder(ctx.resource);
                auto x = interpolate(ctx.frame, 0, 60, 100.0f, 400.0f);
                builder.rect("moving-box", {x, 256.0f, 0.0f}, Color::white());
                return builder.build();
            }
        };
    });

    std::string comp_id = absl::GetFlag(FLAGS_composition);
    if (comp_id.empty()) {
        std::cout << "Usage: chronon3d_cli --composition <id> [flags]" << std::endl;
        std::cout << "Available compositions:" << std::endl;
        for (const auto& name : CompositionRegistry::instance().available()) {
            std::cout << "  " << name << std::endl;
        }
        return 1;
    }

    try {
        if (!CompositionRegistry::instance().contains(comp_id)) {
            spdlog::error("Unknown composition: {}", comp_id);
            return 1;
        }

        auto comp_ptr = std::make_shared<Composition>(CompositionRegistry::instance().create(comp_id));
        spdlog::info("Loaded composition: {} ({}x{})", comp_ptr->name(), comp_ptr->width(), comp_ptr->height());

        i64 start_frame = absl::GetFlag(FLAGS_start);
        i64 end_frame = absl::GetFlag(FLAGS_end);
        bool single_frame = false;
        std::string output_path = absl::GetFlag(FLAGS_output);

        if (absl::GetFlag(FLAGS_frame) != -1) {
            start_frame = absl::GetFlag(FLAGS_frame);
            end_frame = start_frame + 1;
            single_frame = true;
        } else if (end_frame == -1) {
            end_frame = comp_ptr->duration();
        }

        auto renderer = std::make_shared<SoftwareRenderer>();
        RenderPipeline pipeline(comp_ptr, renderer);

        spdlog::info("Rendering frames {} to {}...", start_frame, end_frame);
        
        pipeline.run(start_frame, end_frame, [&](RenderedFrame rf) {
            std::string path = output_path;
            if (!single_frame) {
                std::filesystem::path p(output_path);
                path = (p.parent_path() / fmt::format("{}_{:04d}{}", p.stem().string(), rf.frame, p.extension().string())).string();
            }
            spdlog::info("Saving frame {} to {}...", rf.frame, path);
            rf.framebuffer->save_ppm(path);
            
            FrameMark;
        });

        spdlog::info("Done!");

    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return 1;
    }

    return 0;
}
