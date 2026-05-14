#include "../commands.hpp"
#include <iostream>

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

} // namespace cli
} // namespace chronon3d
