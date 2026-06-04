#pragma once

#include <memory_resource>
#include <string>

namespace chronon3d {

enum class TrackMatteType {
    None,
    Alpha,
    AlphaInverted,
    Luma,
    LumaInverted,
};

struct TrackMatte {
    TrackMatteType   type{TrackMatteType::None};
    std::pmr::string source_layer;

    [[nodiscard]] bool active() const { return type != TrackMatteType::None && !source_layer.empty(); }

    explicit TrackMatte(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : source_layer(res) {}
};

} // namespace chronon3d
