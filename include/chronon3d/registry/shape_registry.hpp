#pragma once

#include <chronon3d/registry/shape_ids.hpp>
#include <chronon3d/registry/shape_params.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <memory_resource>

namespace chronon3d::registry {

enum class ShapeKind {
    Primitive,
    Path,
    Image,
    Mesh,
};

[[nodiscard]] constexpr std::string_view to_string(ShapeKind kind) {
    switch (kind) {
    case ShapeKind::Primitive: return "primitive";
    case ShapeKind::Path:      return "path";
    case ShapeKind::Image:     return "image";
    case ShapeKind::Mesh:      return "mesh";
    }
    return "primitive";
}

struct ShapeDescriptor {
    std::string id;
    std::string display_name;
    ShapeKind   kind{ShapeKind::Primitive};
    std::string description;
    bool        builtin{false};
    using ShapeFactory = std::function<RenderNode(
        std::pmr::memory_resource*,
        std::string,
        ShapeParams)>;
    ShapeFactory factory{};
};

class ShapeRegistry {
public:
    static ShapeRegistry& instance();

    ShapeRegistry();

    void register_shape(ShapeDescriptor descriptor);

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] const ShapeDescriptor& get(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> available() const;
    [[nodiscard]] std::vector<ShapeDescriptor> list() const;
    [[nodiscard]] RenderNode create_node(
        std::string_view id,
        std::pmr::memory_resource* res,
        std::string name,
        ShapeParams params) const;

private:
    std::map<std::string, ShapeDescriptor, std::less<>> m_shapes;
};

} // namespace chronon3d::registry
