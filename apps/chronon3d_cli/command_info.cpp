#include "commands.hpp"
#include <spdlog/spdlog.h>
#include <iostream>

namespace chronon3d {
namespace cli {

int command_info(const CompositionRegistry& registry, const std::string& id) {
    if (!registry.contains(id)) {
        spdlog::error("Unknown composition: {}", id);
        return 1;
    }

    auto comp = registry.create(id);
    std::cout << "Composition: " << id << std::endl;
    std::cout << "  Dimensions: " << comp.width() << "x" << comp.height() << std::endl;
    std::cout << "  Duration:   " << comp.duration() << " frames" << std::endl;
    std::cout << "  Frame Rate: " << comp.frame_rate().numerator << "/" << comp.frame_rate().denominator << " fps" << std::endl;
    return 0;
}

} // namespace cli
} // namespace chronon3d
