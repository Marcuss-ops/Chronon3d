#include "commands.hpp"
#include <chronon3d/core/pipeline.hpp>
#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/io/image_writer.hpp>
#include <spdlog/spdlog.h>
#include <iostream>
#include <filesystem>
#include <fmt/format.h>

namespace chronon3d {
namespace cli {

int command_list(const CompositionRegistry& registry) {
    auto ids = registry.available();
    if (ids.empty()) {
        std::cout << "No compositions registered." << std::endl;
        return 0;
    }

    std::cout << "Available compositions:" << std::endl;
    for (const auto& id : ids) {
        std::cout << "  - " << id << std::endl;
    }
    return 0;
}

int command_info(const CompositionRegistry& registry, const std::string& id) {
    if (!registry.contains(id)) {
        spdlog::error("Unknown composition: {}", id);
        return 1;
    }

    auto comp = registry.create(id);
    std::cout << "Composition: " << id << std::endl;
    std::cout << "  Dimensions: " << comp.width() << "x" << comp.height() << std::endl;
    std::cout << "  Duration:   " << comp.duration() << " frames" << std::endl;
    std::cout << "  Frame Rate: " << comp.frame_rate().num << "/" << comp.frame_rate().den << " fps" << std::endl;
    return 0;
}

int command_render(const CompositionRegistry& registry, const RenderArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }

    auto comp = std::make_shared<Composition>(registry.create(args.comp_id));
    auto renderer = std::make_shared<SoftwareRenderer>();
    RenderPipeline pipeline(comp, renderer);

    i64 start_frame = args.start;
    i64 end_frame = args.end;
    bool single_frame = false;

    if (args.frame != -1) {
        start_frame = args.frame;
        end_frame = start_frame + 1;
        single_frame = true;
    } else {
        if (start_frame == -1) start_frame = 0;
        if (end_frame == -1) end_frame = comp->duration();
    }

    spdlog::info("Rendering {} [{} -> {})...", args.comp_id, start_frame, end_frame);

    pipeline.run(start_frame, end_frame, [&](RenderedFrame rf) {
        std::string path = args.output;
        if (!single_frame) {
            std::filesystem::path p(args.output);
            path = (p.parent_path() / fmt::format("{}_{:04d}{}", p.stem().string(), rf.frame, p.extension().string())).string();
        }
        
        // Ensure directory exists
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }

        if (save_png(*rf.framebuffer, path)) {
            spdlog::info("Frame {} saved to {}", rf.frame, path);
        } else {
            spdlog::error("Failed to save frame {} to {}", rf.frame, path);
        }
    });

    spdlog::info("Render complete.");
    return 0;
}

} // namespace cli
} // namespace chronon3d
