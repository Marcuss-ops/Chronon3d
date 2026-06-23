#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::renderer {

// Forward declarations for shape processors
std::unique_ptr<ShapeProcessor> create_shape_processor();
std::unique_ptr<ShapeProcessor> create_image_processor();
std::unique_ptr<ShapeProcessor> create_tiled_image_processor();
std::unique_ptr<ShapeProcessor> create_grid_background_processor();
std::unique_ptr<ShapeProcessor> create_mesh_processor();
std::unique_ptr<ShapeProcessor> create_line_processor();
#ifdef CHRONON3D_USE_BLEND2D
std::unique_ptr<ShapeProcessor> create_path_processor();
#endif
std::unique_ptr<ShapeProcessor> create_fake_box3d_processor();
std::unique_ptr<ShapeProcessor> create_grid_plane_processor();
#ifdef CHRONON3D_ENABLE_TEXT
std::unique_ptr<ShapeProcessor> create_text_processor();
std::unique_ptr<ShapeProcessor> create_text_run_processor();
#endif

// Forward declarations for effect processors
std::unique_ptr<EffectProcessor> create_blur_effect_processor();
std::unique_ptr<EffectProcessor> create_tint_effect_processor();
std::unique_ptr<EffectProcessor> create_fake_3d_wave_effect_processor();
std::unique_ptr<EffectProcessor> create_exposure_effect_processor();
std::unique_ptr<EffectProcessor> create_levels_effect_processor();
std::unique_ptr<EffectProcessor> create_fill_effect_processor();
std::unique_ptr<EffectProcessor> create_noise_effect_processor();
std::unique_ptr<EffectProcessor> create_offset_effect_processor();
std::unique_ptr<EffectProcessor> create_curves_effect_processor();
std::unique_ptr<EffectProcessor> create_stroke_effect_processor();
std::unique_ptr<EffectProcessor> create_radial_blur_effect_processor();
std::unique_ptr<EffectProcessor> create_directional_blur_effect_processor();

void register_builtin_processors(SoftwareRegistry& registry) {
    // Shapes
    auto shape_proc = create_shape_processor();
    registry.register_shape(ShapeType::Rect, std::move(shape_proc));
    registry.register_shape(ShapeType::Circle, create_shape_processor());
    registry.register_shape(ShapeType::RoundedRect, create_shape_processor());

    registry.register_shape(ShapeType::Line, create_line_processor());
#ifdef CHRONON3D_USE_BLEND2D
    registry.register_shape(ShapeType::Path, create_path_processor());
#endif
    registry.register_shape(ShapeType::Image, create_image_processor());
    registry.register_shape(ShapeType::TiledImage, create_tiled_image_processor());
    registry.register_shape(ShapeType::GridBackground, create_grid_background_processor());
#ifdef CHRONON3D_ENABLE_TEXT
    // TODO(P2 — Text pipeline clean-up; remove in two coordinated steps):
    //
    //   Step A. DELETE the legacy text producer quartet:
    //     - `RenderNodeFactory::text()` in src/scene/model/render_node_factory.cpp
    //       (currently unreachable from production; only 2 tests in
    //       tests/scene/rendering/test_render_node_factory.cpp:104,116
    //       still call it directly — migrate those test cases to
    //       RenderNodeFactory::text_run())
    //     - `create_text_processor()` factory + its forward declaration in
    //       this file.
    //     - The `ShapeType::Text` enum entry in
    //       include/chronon3d/scene/model/shape/shape.hpp (case 6 in the
    //       dispatch ladder; case 14 is TextRun). Cascading migration
    //       touches shape_rasterizer.cpp:56, shape_rasterizer_helpers.hpp:108,
    //       render_graph_hashing.hpp:307, graph_builder_source_pass.cpp:124,
    //       analysis_helpers.hpp:53,102, text_audit_engine.cpp:501, test_shape_model.cpp:84,
    //       and 2 sites in tests/renderer/helpers/test_stroke_gradient_helpers.cpp.
    //     - The `case 6:  return ShapeType::Text;` and inverse maps in
    //       shape.hpp:381,402.
    //
    //   Step B. DELETE the orphaned factory call site:
    //     - `factory = make_factory<TextSpec>(...RenderNodeFactory::text())`
    //       already gone from src/registry/shape_registry.cpp:108-117
    //       (P1 commit). No further action.
    //
    // Until then, this registration keeps the orphan ShapeType::Text
    // dispatch ladder compiling while the authoring layer routes every
    // text node through TextRun.
    registry.register_shape(ShapeType::Text, create_text_processor());
    registry.register_shape(ShapeType::TextRun, create_text_run_processor());
#endif
    registry.register_shape(ShapeType::Mesh, create_mesh_processor());
    registry.register_shape(ShapeType::FakeBox3D, create_fake_box3d_processor());
    registry.register_shape(ShapeType::GridPlane, create_grid_plane_processor());

    // Effects
    registry.register_effect_processor<BlurParams>(create_blur_effect_processor());
    registry.register_effect_processor<TintParams>(create_tint_effect_processor());
    registry.register_effect_processor<Fake3DWaveParams>(create_fake_3d_wave_effect_processor());
    registry.register_effect_processor<ExposureParams>(create_exposure_effect_processor());
    registry.register_effect_processor<LevelsParams>(create_levels_effect_processor());
    registry.register_effect_processor<FillParams>(create_fill_effect_processor());
    registry.register_effect_processor<NoiseParams>(create_noise_effect_processor());
    registry.register_effect_processor<OffsetParams>(create_offset_effect_processor());
    registry.register_effect_processor<DirectionalBlurParams>(create_directional_blur_effect_processor());
    registry.register_effect_processor<RadialBlurParams>(create_radial_blur_effect_processor());
    registry.register_effect_processor<StrokeParams>(create_stroke_effect_processor());
    registry.register_effect_processor<CurvesParams>(create_curves_effect_processor());
}

} // namespace chronon3d::renderer
