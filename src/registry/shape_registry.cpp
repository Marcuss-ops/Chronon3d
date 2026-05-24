#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/scene/render_node_factory.hpp>

#include <stdexcept>
#include <utility>
#include <variant>

namespace chronon3d::registry {

namespace {

template <typename T, typename Fn>
ShapeDescriptor::ShapeFactory make_factory(Fn&& fn) {
    return [fn = std::forward<Fn>(fn)](
               std::pmr::memory_resource* res,
               std::string name,
               ShapeParams params) -> RenderNode {
        try {
            return fn(res, std::move(name), std::get<T>(std::move(params)));
        } catch (const std::bad_variant_access&) {
            throw std::runtime_error("Invalid params for shape factory");
        }
    };
}

void register_builtin_shapes(ShapeRegistry& registry) {
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::Rect},
        .display_name = "Rectangle",
        .kind = ShapeKind::Primitive,
        .description = "Axis-aligned filled rectangle",
        .builtin = true,
        .factory = make_factory<RectParams>([](auto* res, std::string name, const RectParams& p) {
            return RenderNodeFactory::rect(res, std::move(name), p);
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::RoundedRect},
        .display_name = "Rounded Rectangle",
        .kind = ShapeKind::Primitive,
        .description = "Rectangle with rounded corners",
        .builtin = true,
        .factory = make_factory<RoundedRectParams>([](auto* res, std::string name, const RoundedRectParams& p) {
            return RenderNodeFactory::rounded_rect(res, std::move(name), p);
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::Circle},
        .display_name = "Circle",
        .kind = ShapeKind::Primitive,
        .description = "Filled circle or ellipse",
        .builtin = true,
        .factory = make_factory<CircleParams>([](auto* res, std::string name, const CircleParams& p) {
            return RenderNodeFactory::circle(res, std::move(name), p);
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::Line},
        .display_name = "Line",
        .kind = ShapeKind::Primitive,
        .description = "Single segment with thickness",
        .builtin = true,
        .factory = make_factory<LineParams>([](auto* res, std::string name, const LineParams& p) {
            return RenderNodeFactory::line(res, std::move(name), p);
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::Path},
        .display_name = "Bezier Path",
        .kind = ShapeKind::Path,
        .description = "Vector path that can be stroked or filled",
        .builtin = true,
        .factory = make_factory<PathParams>([](auto* res, std::string name, PathParams p) {
            return RenderNodeFactory::path(res, std::move(name), std::move(p));
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::Image},
        .display_name = "Image",
        .kind = ShapeKind::Image,
        .description = "Bitmap image source",
        .builtin = true,
        .factory = make_factory<ImageParams>([](auto* res, std::string name, const ImageParams& p) {
            return RenderNodeFactory::image(res, std::move(name), p);
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::TiledImage},
        .display_name = "Tiled Image",
        .kind = ShapeKind::Image,
        .description = "Repeating bitmap pattern",
        .builtin = true,
        .factory = make_factory<ImageParams>([](auto* res, std::string name, const ImageParams& p) {
            return RenderNodeFactory::tiled_image(res, std::move(name), p);
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::GridBackground},
        .display_name = "Grid Background",
        .kind = ShapeKind::Primitive,
        .description = "Native procedural full-screen grid background",
        .builtin = true,
        .factory = make_factory<GridBackgroundParams>([](auto* res, std::string name, const GridBackgroundParams& p) {
            return RenderNodeFactory::grid_background(res, std::move(name), p);
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::Text},
        .display_name = "Text",
        .kind = ShapeKind::Primitive,
        .description = "Rasterized text",
        .builtin = true,
        .factory = make_factory<TextParams>([](auto* res, std::string name, TextParams p) {
            return RenderNodeFactory::text(res, std::move(name), std::move(p));
        }),
    });
    registry.register_shape(ShapeDescriptor{
        .id = std::string{shape_ids::Mesh},
        .display_name = "Mesh",
        .kind = ShapeKind::Mesh,
        .description = "Mesh-backed 3D/2.5D shape",
        .builtin = true,
    });
}

} // namespace

ShapeRegistry& ShapeRegistry::instance() {
    static ShapeRegistry registry;
    return registry;
}

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

RenderNode ShapeRegistry::create_node(
    std::string_view id,
    std::pmr::memory_resource* res,
    std::string name,
    ShapeParams params) const {
    const auto& descriptor = get(id);
    if (!descriptor.factory) {
        throw std::runtime_error("Shape has no factory: " + std::string{id});
    }
    return descriptor.factory(res, std::move(name), std::move(params));
}

} // namespace chronon3d::registry
