#include "../../commands.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace chronon3d::cli {

namespace {

std::string format_size(const CompositionDescriptor& descriptor) {
    if (!descriptor.width || !descriptor.height) return "-";
    return std::to_string(*descriptor.width) + "×" + std::to_string(*descriptor.height);
}

std::string format_fps(const CompositionDescriptor& descriptor) {
    if (!descriptor.fps) return "-";
    std::ostringstream out;
    if (descriptor.fps->denominator == 1) {
        out << descriptor.fps->numerator << "fps";
    } else {
        out << descriptor.fps->numerator << "/"
            << descriptor.fps->denominator << "fps";
    }
    return out.str();
}

std::string format_duration(const CompositionDescriptor& descriptor) {
    return descriptor.duration
        ? std::to_string(descriptor.duration->integral()) + "f"
        : "-";
}

} // namespace

int command_list(const CompositionRegistry& registry) {
    const auto descriptors = registry.descriptors();
    if (descriptors.empty()) {
        std::cout << "No compositions registered.\n";
        return 0;
    }

    std::cout << std::left
              << std::setw(30) << "ID"
              << std::setw(14) << "SIZE"
              << std::setw(14) << "FPS"
              << std::setw(10) << "FRAMES"
              << "CATEGORY\n";
    std::cout << std::string(82, '-') << '\n';

    for (const auto& descriptor : descriptors) {
        std::cout << std::left
                  << std::setw(30) << descriptor.id
                  << std::setw(14) << format_size(descriptor)
                  << std::setw(14) << format_fps(descriptor)
                  << std::setw(10) << format_duration(descriptor)
                  << (descriptor.category.empty() ? "Uncategorized" : descriptor.category)
                  << '\n';
    }
    return 0;
}

} // namespace chronon3d::cli
