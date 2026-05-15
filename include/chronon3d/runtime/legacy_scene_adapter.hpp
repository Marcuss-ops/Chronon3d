#pragma once

#include <chronon3d/runtime/evaluated_scene.hpp>
#include <chronon3d/scene/scene.hpp>

namespace chronon3d {

class LegacySceneAdapter {
public:
    [[nodiscard]] Scene convert(const EvaluatedScene& evaluated,
                                std::pmr::memory_resource* res = std::pmr::get_default_resource()) const;
};

} // namespace chronon3d
