#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <fmt/format.h>
#include <vector>

using namespace chronon3d;

/**
 * ProjectedPlaneValidation
 * Renders the ground truth cases for 2.5D perspective projection.
 */
static Composition ProjectedPlaneValidation() {
    return composition({
        .name = "ProjectedPlaneValidation",
        .width = 1280,
        .height = 720,
        .duration = 7 // 7 cases
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Fixed Camera
        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .projection_mode = Camera2_5DProjectionMode::Fov,
            .fov_deg = 45.0f
        });

        // 1. Background
        s.layer("bg", [](LayerBuilder& l) {
            l.position({0, 0, 1000});
            l.rect("bg", RectParams{.size={5000, 5000}, .color=Color{0.1f, 0.1f, 0.12f, 1.0f}});
        });

        struct TestCase {
            std::string name;
            Vec3 rotation;
        };

        const std::vector<TestCase> cases = {
            {"base",  {0, 0, 0}},
            {"y30",   {0, 30, 0}},
            {"y60",   {0, 60, 0}},
            {"x30",   {30, 0, 0}},
            {"z45",   {0, 0, 45}},
            {"xy",    {30, 30, 0}},
            {"xyz",   {20, 40, 15}}
        };

        const auto& test = cases[std::clamp<size_t>(ctx.frame, 0, cases.size() - 1)];

        // 1. Background Grid (Deeper in Z)
        s.layer("background_grid", [&](LayerBuilder& l) {
            l.position({0, 0, 200})
             .enable_3d(true);
            
            // Draw a large grid of lines
            const f32 grid_size = 2000.0f;
            const f32 step = 100.0f;
            for (f32 x = -grid_size/2; x <= grid_size/2; x += step) {
                l.rect(fmt::format("bg_vgrid_{}", x), RectParams{.size={1, grid_size}, .color=Color{0.3f, 0.3f, 0.3f, 0.5f}, .pos={x, 0, 0}});
            }
            for (f32 y = -grid_size/2; y <= grid_size/2; y += step) {
                l.rect(fmt::format("bg_hgrid_{}", y), RectParams{.size={grid_size, 1}, .color=Color{0.3f, 0.3f, 0.3f, 0.5f}, .pos={0, y, 0}});
            }
        });
        
        // 1.5 Floor Grid (X-Z plane)
        s.layer("floor", [&](LayerBuilder& l) {
            l.position({0, 400, 500})
             .rotate({90, 0, 0})
             .enable_3d(true);
            
            const f32 grid_size = 2000.0f;
            const f32 step = 100.0f;
            for (f32 x = -grid_size/2; x <= grid_size/2; x += step) {
                l.rect(fmt::format("f_vgrid_{}", x), RectParams{.size={2, grid_size}, .color=Color{0.4f, 0.4f, 0.8f, 0.3f}, .pos={x, 0, 0}});
            }
            for (f32 z = -grid_size/2; z <= grid_size/2; z += step) {
                l.rect(fmt::format("f_hgrid_{}", z), RectParams{.size={grid_size, 2}, .color=Color{0.4f, 0.4f, 0.8f, 0.3f}, .pos={0, z, 0}});
            }
        });

        // 2. Validation Card
        s.layer("validation_card", [&](LayerBuilder& l) {
            l.position({0, 0, 0})
             .rotate(test.rotation)
             .enable_3d(true)
             .rect("card", RectParams{
                 .size = {300, 200}, 
                 .color = Color::white(),
                 .pos = {0, 0, 0}
             });
            
            // Grid Lines
            for (int i = -150; i <= 150; i += 50) {
                l.rect(fmt::format("vgrid_{}", i), RectParams{.size={1, 200}, .color=Color{0,0,0,0.5f}, .pos={(f32)i, 0, 0}});
            }
            for (int j = -100; j <= 100; j += 50) {
                l.rect(fmt::format("hgrid_{}", j), RectParams{.size={300, 1}, .color=Color{0,0,0,0.5f}, .pos={0, (f32)j, 0}});
            }

            // X-axis (Red)
            l.rect("axis_x", RectParams{.size={150, 4}, .color=Color::red(), .pos={75, 0, 0}});
            // Y-axis (Green)
            l.rect("axis_y", RectParams{.size={4, 100}, .color=Color::green(), .pos={0, 50, 0}});
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProjectedPlaneValidation", ProjectedPlaneValidation)
