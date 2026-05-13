#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>

namespace chronon3d::registry {

enum class ShapeKind {
    Primitive,
    Path,
    Text,
    Image,
    Mesh,
};

[[nodiscard]] constexpr std::string_view to_string(ShapeKind kind) {
    switch (kind) {
    case ShapeKind::Primitive: return "primitive";
    case ShapeKind::Path:      return "path";
    case ShapeKind::Text:      return "text";
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
};

class ShapeRegistry {
public:
    ShapeRegistry();

    void register_shape(ShapeDescriptor descriptor);

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] const ShapeDescriptor& get(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> available() const;
    [[nodiscard]] std::vector<ShapeDescriptor> list() const;

private:
    std::map<std::string, ShapeDescriptor, std::less<>> m_shapes;
};

} // namespace chronon3d::registry
