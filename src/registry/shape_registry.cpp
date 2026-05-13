#include <chronon3d/registry/shape_registry.hpp>

#include <stdexcept>
#include <utility>

namespace chronon3d::registry {

namespace {

void register_builtin_shapes(ShapeRegistry& registry) {
    registry.register_shape(ShapeDescriptor{
        .id = "shape.rect",
        .display_name = "Rectangle",
        .kind = ShapeKind::Primitive,
        .description = "Axis-aligned filled rectangle",
        .builtin = true,
    });
    registry.register_shape(ShapeDescriptor{
        .id = "shape.rounded_rect",
        .display_name = "Rounded Rectangle",
        .kind = ShapeKind::Primitive,
        .description = "Rectangle with rounded corners",
        .builtin = true,
    });
    registry.register_shape(ShapeDescriptor{
        .id = "shape.circle",
        .display_name = "Circle",
        .kind = ShapeKind::Primitive,
        .description = "Filled circle or ellipse",
        .builtin = true,
    });
    registry.register_shape(ShapeDescriptor{
        .id = "shape.line",
        .display_name = "Line",
        .kind = ShapeKind::Primitive,
        .description = "Single segment with thickness",
        .builtin = true,
    });
    registry.register_shape(ShapeDescriptor{
        .id = "shape.path",
        .display_name = "Bezier Path",
        .kind = ShapeKind::Path,
        .description = "Vector path that can be stroked or filled",
        .builtin = true,
    });
    registry.register_shape(ShapeDescriptor{
        .id = "shape.text",
        .display_name = "Text",
        .kind = ShapeKind::Text,
        .description = "Glyph-shaped text object",
        .builtin = true,
    });
    registry.register_shape(ShapeDescriptor{
        .id = "shape.image",
        .display_name = "Image",
        .kind = ShapeKind::Image,
        .description = "Bitmap image source",
        .builtin = true,
    });
    registry.register_shape(ShapeDescriptor{
        .id = "shape.mesh",
        .display_name = "Mesh",
        .kind = ShapeKind::Mesh,
        .description = "Mesh-backed 3D/2.5D shape",
        .builtin = true,
    });
}

} // namespace

ShapeRegistry::ShapeRegistry() {
    register_builtin_shapes(*this);
}

void ShapeRegistry::register_shape(ShapeDescriptor descriptor) {
    if (descriptor.id.empty()) {
        throw std::runtime_error("Shape id cannot be empty");
    }
    if (m_shapes.contains(descriptor.id)) {
        throw std::runtime_error("Duplicate shape: " + descriptor.id);
    }
    if (descriptor.display_name.empty()) {
        descriptor.display_name = descriptor.id;
    }
    const std::string id = descriptor.id;
    m_shapes.emplace(id, std::move(descriptor));
}

bool ShapeRegistry::contains(std::string_view id) const {
    return m_shapes.find(id) != m_shapes.end();
}

const ShapeDescriptor& ShapeRegistry::get(std::string_view id) const {
    auto it = m_shapes.find(id);
    if (it == m_shapes.end()) {
        throw std::runtime_error("Unknown shape: " + std::string{id});
    }
    return it->second;
}

std::vector<std::string> ShapeRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_shapes.size());
    for (const auto& [id, _] : m_shapes) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<ShapeDescriptor> ShapeRegistry::list() const {
    std::vector<ShapeDescriptor> descriptors;
    descriptors.reserve(m_shapes.size());
    for (const auto& [_, descriptor] : m_shapes) {
        descriptors.push_back(descriptor);
    }
    return descriptors;
}

} // namespace chronon3d::registry
