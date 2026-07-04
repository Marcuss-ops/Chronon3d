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
// M1.5#9 (3/5) — `create_text_processor()` factory + forward-declaration
// REMOVED.  The orphan `ShapeType::Text` dispatch ladder is no longer registered
// with the SoftwareRegistry; the canonical text pipeline is exclusively
// `ShapeType::TextRun` → `SoftwareBackend::draw_text_run` via
// `multi_source_node` / `TextRunNode`.  Step 4 (M1.5#9 4/5) will delete the
// orphan definition in `src/backends/software/processors/text/software_text_processor.cpp`
// along with the rest of the `processors/text/` directory tree.  See
// `docs/FOLLOWUP_TICKETS.md` §TICKET-M1.5#9-SEQUENCE for the canonical 5-step
// plan; this comment + the registration removal closes step 3.

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
    // M1.5#9 (3/5) — orphan `registry.register_shape(ShapeType::Text, create_text_processor())`
    // removed.  The factory is no longer exposed via SoftwareRegistry.  Note
    // the orphan `create_text_processor()` definition in
    // `src/backends/software/processors/text/software_text_processor.cpp:314`
    // remains compile-clean (exported symbol, no callers post-this-commit;
    // will be deleted wholesale with the rest of the `processors/text/`
    // directory tree in step 4).  This change closes M1.5#9 step 3 with
    // zero ABI impact (factory symbol was non-public) + zero source-side
    // regression (no caller sites to mutate).  The orphan `ShapeType::Text`
    // enum entry + the cascade in shape_rasterizer.cpp/shape_rasterizer_helpers.hpp/
    // render_graph_hashing.hpp/graph_builder_source_pass.cpp/analysis_helpers.hpp
    // remain intact via direct `Shape::set_type(ShapeType::Text)` callers
    // (chiefly test_fixtures that test the Shape data model without going
    // through SoftwareRegistry) — those will be cleaned in M1.5#9 step 4 +
    // a separate `ShapeType::Text` enum-retirement ticket.
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
