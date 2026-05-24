#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::api {

struct BackgroundPresetDescriptor {
    std::string_view id;
    std::string_view name;
    std::string_view family;
    std::string_view description;
    f32 duration_seconds{};
    i32 fps{30};
    bool loopable{true};
    bool realtime_safe{true};
};

struct BackgroundOptions {
    Color background{0.01f, 0.012f, 0.025f, 1.0f};
    Color accent{0.25f, 0.45f, 1.0f, 1.0f};
    Color glow{0.20f, 0.40f, 1.0f, 0.25f};
    f32 intensity{1.0f};
    f32 speed{1.0f};
    bool animated{true};
};

struct BackgroundCatalog {
    std::vector<BackgroundPresetDescriptor> presets;
};

[[nodiscard]] const BackgroundCatalog& builtin_background_catalog();
[[nodiscard]] const BackgroundPresetDescriptor* find_builtin_background(std::string_view id);
[[nodiscard]] std::string builtin_background_catalog_json();

} // namespace chronon3d::api

extern "C" {

[[nodiscard]] const char* chronon3d_background_catalog_json();

}
