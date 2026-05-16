#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::renderer {

// Forward declarations for shape processors
std::unique_ptr<ShapeProcessor> create_shape_processor();
std::unique_ptr<ShapeProcessor> create_text_processor();
std::unique_ptr<ShapeProcessor> create_image_processor();
std::unique_ptr<ShapeProcessor> create_mesh_processor();
std::unique_ptr<ShapeProcessor> create_line_processor();
std::unique_ptr<ShapeProcessor> create_fake_extruded_text_processor();
std::unique_ptr<ShapeProcessor> create_fake_box3d_processor();
std::unique_ptr<ShapeProcessor> create_grid_plane_processor();

// Forward declarations for effect processors
std::unique_ptr<EffectProcessor> create_blur_effect_processor();
std::unique_ptr<EffectProcessor> create_tint_effect_processor();

void register_builtin_processors(SoftwareRegistry& registry) {
    // Shapes
    auto shape_proc = create_shape_processor();
    registry.register_shape(ShapeType::Rect, std::move(shape_proc));
    registry.register_shape(ShapeType::Circle, create_shape_processor());
    registry.register_shape(ShapeType::RoundedRect, create_shape_processor());
    
    registry.register_shape(ShapeType::Line, create_line_processor());
    registry.register_shape(ShapeType::Text, create_text_processor());
    registry.register_shape(ShapeType::Image, create_image_processor());
    registry.register_shape(ShapeType::Mesh, create_mesh_processor());
    registry.register_shape(ShapeType::FakeExtrudedText, create_fake_extruded_text_processor());
    registry.register_shape(ShapeType::FakeBox3D, create_fake_box3d_processor());
    registry.register_shape(ShapeType::GridPlane, create_grid_plane_processor());

    // Effects
    registry.register_effect_processor<BlurParams>(create_blur_effect_processor());
    registry.register_effect_processor<TintParams>(create_tint_effect_processor());
}

} // namespace chronon3d::renderer
