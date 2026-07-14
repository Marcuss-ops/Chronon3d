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
// ─── P1-7 Chore 1 (commit A) ─────────────────────────────────────────────
// M1.5#9 steps 3 + 4 — DONE.
//   • Step 3 (commit pending this session): `create_text_processor()` factory
//     forward-declaration + `registry.register_shape(ShapeType::Text, ...)`
//     REMOVED above and below (orphan dispatch ladder).
//   • Step 4 (this commit): the orphan `create_text_processor()` definition +
//     the `src/backends/software/processors/text/` directory tree (7 source
//     files + CMakeLists.txt = 8 in total) deleted wholesale.
// Canonical text pipeline is exclusively `ShapeType::TextRun` →
// `SoftwareBackend::draw_text_run` via `multi_source_node` / `TextRunNode`.
// The `ShapeType::Text` enum value (~15 downstream consumer files still key
// off it: `shape_rasterizer.cpp`, `render_graph_hashing.hpp`,
// `graph_builder_source_pass.cpp`, `analysis_helpers.hpp`,
// `test_shape_model.cpp`, etc.) is preserved for the upcoming
// `ShapeType::Text` enum-retirement ticket (separate scope).
// See `docs/tickets/TICKET-M1.5#9-SEQUENCE.md` for the canonical 5-step
// plan; steps 3 + 4 closed by commit A.

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
    // P1-7 Chore 1 (commit A) — M1.5#9 steps 3 + 4 BOTH done.
    //   • Step 3 (prior commit): `registry.register_shape(ShapeType::Text,
    //     create_text_processor())` removed.  No new registration line.
    //   • Step 4 (this commit): the orphan `create_text_processor()`
    //     definition + the entire `src/backends/software/processors/text/`
    //     directory tree deleted wholesale (8 files: software_text_processor.cpp,
    //     software_text_effects.{cpp,hpp}, text_glow.cpp, text_shadow.cpp,
    //     text_effects.hpp, text_processor_helpers.hpp, CMakeLists.txt).
    // Net ABI impact: zero (factory was non-public; deletion is in-tree only).
    // The `ShapeType::Text` enum entry remains intact (separate
    // enum-retirement ticket, ~15 downstream consumers still key off it).
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
