#pragma once

#include <chronon3d/core/types/types.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::api {

struct AnimationClipDescriptor {
    std::string_view id;
    std::string_view name;
    std::string_view family;
    std::string_view description;
    f32 duration_seconds{};
    i32 fps{30};
    bool loopable{true};
    bool realtime_safe{true};
};

struct AnimationCatalog {
    std::vector<AnimationClipDescriptor> clips;
};

[[nodiscard]] const AnimationCatalog& builtin_animation_catalog();
[[nodiscard]] const AnimationClipDescriptor* find_builtin_animation(std::string_view id);
[[nodiscard]] std::string builtin_animation_catalog_json();

} // namespace chronon3d::api

extern "C" {

[[nodiscard]] const char* chronon3d_animation_catalog_json();

}
