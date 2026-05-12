#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/render_node.hpp>
#include <string>
#include <vector>
#include <memory_resource>

namespace chronon3d {

struct Layer {
    std::pmr::string name;
    Transform transform{};
    Frame from{0};
    Frame duration{-1};
    bool visible{true};
    bool is_3d{false};
    std::pmr::vector<RenderNode> nodes;

    explicit Layer(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res), nodes(res) {}

    [[nodiscard]] bool active_at(Frame frame) const {
        if (!visible) return false;
        if (frame < from) return false;
        if (duration < 0) return true;
        return frame < from + duration;
    }
};

} // namespace chronon3d
