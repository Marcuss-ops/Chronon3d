#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <chronon3d/registry/shape_ids.hpp>
#include <chronon3d/registry/shape_params.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
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

[[nodiscard]] inline std::string to_string(ShapeKind kind) {
    return enum_utils::enum_name_lower_snake(kind);
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

    /// Clear all registered shapes (used in tests for clean reset).
    void clear();

    /// Clear all shapes and re-register built-in shapes.
    void reset();

private:
    std::map<std::string, ShapeDescriptor, std::less<>> m_shapes;
};

/// Create a ShapeRegistry pre-loaded with all built-in shapes.
/// The constructor is empty; callers must either use this helper
/// or call `register_builtin_shapes()` explicitly.
ShapeRegistry make_default_shape_registry();

} // namespace chronon3d::registry
