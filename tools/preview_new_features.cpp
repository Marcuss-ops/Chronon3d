#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/fill.hpp>
#include <filesystem>
#include <cstdio>

using namespace chronon3d;

static SoftwareRenderer g_rend;

static std::unique_ptr<Framebuffer> render(int w, int h,
    std::function<void(SceneBuilder&)> fn)
{
    Composition comp(CompositionSpec{.width = w, .height = h, .duration = 1},
        [&](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            fn(s);
            return s.build();
        });
    return g_rend.render_frame(comp, 0);
}

static void save(const Framebuffer& fb, const char* path) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    save_png(fb, path);
    std::printf("  saved %s\n", path);
}

// ---------------------------------------------------------------------------
int main() {
    const char* out = "output/new_features";
    std::printf("Rendering new feature previews → %s/\n\n", out);

    // ------------------------------------------------------------------
    // 1. Linear gradient fill — red → blue across a rect
    // ------------------------------------------------------------------
    std::printf("[1/7] linear gradient fill (rect)\n");
    {
        auto fb = render(400, 120, [](SceneBuilder& s) {
            s.rect("r", {
                .size  = {380, 100},
                .color = {1, 1, 1, 1},
                .pos   = {200, 60, 0},
                .fill  = Fill::linear({0,0},{1,0},{
                    {0.0f, Color{1, 0.08f, 0.08f, 1}},
                    {0.5f, Color{1, 0.80f, 0.00f, 1}},
                    {1.0f, Color{0.08f, 0.35f, 1, 1}},
                }),
            });
        });
        save(*fb, "output/new_features/01_gradient_linear_rect.png");
    }

    // ------------------------------------------------------------------
    // 2. Radial gradient fill on a circle
    // ------------------------------------------------------------------
    std::printf("[2/7] radial gradient fill (circle)\n");
    {
        auto fb = render(200, 200, [](SceneBuilder& s) {
            s.circle("c", {
                .radius = 90.0f,
                .color  = {1, 1, 1, 1},
                .pos    = {100, 100, 0},
                .fill   = Fill::radial({0.5f, 0.5f}, 0.5f, {
                    {0.0f, Color{1.0f, 0.95f, 0.4f, 1}},
                    {0.7f, Color{1.0f, 0.35f, 0.0f, 1}},
                    {1.0f, Color{0.1f, 0.0f, 0.0f, 0}},
                }),
            });
        });
        save(*fb, "output/new_features/02_gradient_radial_circle.png");
    }

    // ------------------------------------------------------------------
    // 3. Gradient rounded rect — green → purple
    // ------------------------------------------------------------------
    std::printf("[3/7] gradient fill (rounded rect)\n");
    {
        auto fb = render(300, 120, [](SceneBuilder& s) {
            s.rounded_rect("r", {
                .size   = {280, 100},
                .radius = 20.0f,
                .color  = {1, 1, 1, 1},
                .pos    = {150, 60, 0},
                .fill   = Fill::linear({0,0},{1,0},{
                    {0.0f, Color{0.1f, 0.85f, 0.4f, 1}},
                    {1.0f, Color{0.6f, 0.1f, 0.9f, 1}},
                }),
            });
        });
        save(*fb, "output/new_features/03_gradient_rounded_rect.png");
    }

    // ------------------------------------------------------------------
    // 4. Trim path — four lines at 0%, 33%, 66%, 100%
    // ------------------------------------------------------------------
    std::printf("[4/7] trim path — four trim levels\n");
    {
        auto fb = render(400, 180, [](SceneBuilder& s) {
            // dark background
            s.rect("bg", {.size={400,180}, .color={0.1f,0.1f,0.1f,1}, .pos={200,90,0}});

            const float trims[] = {0.0f, 0.33f, 0.66f, 1.0f};
            const Color col{0.2f, 0.8f, 1.0f, 1};
            for (int i = 0; i < 4; ++i) {
                float y = 35.0f + i * 38.0f;
                s.line("l" + std::to_string(i), {
                    .from   = {30, y, 0},
                    .to     = {370, y, 0},
                    .thickness = 1.0f,
                    .color  = col,
                    .stroke = {.trim_start = 0.0f, .trim_end = trims[i]},
                });
            }
        });
        save(*fb, "output/new_features/04_trim_path_levels.png");
    }

    // ------------------------------------------------------------------
    // 5. Trim path animation — 8 frames, trim_end 0→1
    // ------------------------------------------------------------------
    std::printf("[5/7] trim path — animated strip (8 frames)\n");
    {
        auto fb = render(400, 60, [](SceneBuilder& s) {
            s.rect("bg", {.size={400,60}, .color={0.08f,0.08f,0.08f,1}, .pos={200,30,0}});
            for (int i = 0; i < 8; ++i) {
                float x0 = 10.0f + i * 48.0f;
                float te  = (i + 1) / 8.0f;
                s.line("l" + std::to_string(i), {
                    .from   = {x0, 30, 0},
                    .to     = {x0 + 38.0f, 30, 0},
                    .color  = {1, 0.6f, 0.1f, 1},
                    .stroke = {.trim_start=0, .trim_end=te},
                });
            }
        });
        save(*fb, "output/new_features/05_trim_path_animation_strip.png");
    }

    // ------------------------------------------------------------------
    // 6. 3D rotation — flat vs Y-45 vs Y-90
    // ------------------------------------------------------------------
    std::printf("[6/7] 3D layer rotation — flat / Y45 / Y90\n");
    {
        RenderSettings s3d;
        s3d.use_modular_graph = true;
        g_rend.set_settings(s3d);

        Camera2_5D cam;
        cam.enabled  = true;
        cam.position = {0, 0, -1000};
        cam.zoom     = 1000.0f;

        auto make = [&](Vec3 rot_deg) {
            return Composition(CompositionSpec{.width=200,.height=200,.duration=1},
                [cam, rot_deg](const FrameContext& ctx) {
                    SceneBuilder s(ctx);
                    s.camera().set(cam);
                    s.layer("card", [rot_deg](LayerBuilder& l) {
                        l.enable_3d().position({0,0,0}).rotate(rot_deg);
                        l.rect("r", {.size={140,140},.color={0.2f,0.6f,1,1},.pos={0,0,0}});
                    });
                    return s.build();
                });
        };

        save(*g_rend.render_frame(make({0,0,0}),   0), "output/new_features/06a_rotation_flat.png");
        save(*g_rend.render_frame(make({0,45,0}),  0), "output/new_features/06b_rotation_y45.png");
        save(*g_rend.render_frame(make({0,90,0}),  0), "output/new_features/06c_rotation_y90.png");

        RenderSettings plain;
        g_rend.set_settings(plain);
    }

    // ------------------------------------------------------------------
    // 7. Combined — gradient card with 3D tilt
    // ------------------------------------------------------------------
    std::printf("[7/7] gradient card with X-tilt\n");
    {
        RenderSettings s3d;
        s3d.use_modular_graph = true;
        g_rend.set_settings(s3d);

        Camera2_5D cam;
        cam.enabled  = true;
        cam.position = {0, 0, -1000};
        cam.zoom     = 1000.0f;

        Composition comp(CompositionSpec{.width=400,.height=300,.duration=1},
            [cam](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.camera().set(cam);
                s.layer("card", [](LayerBuilder& l) {
                    l.enable_3d().position({0,0,0}).rotate({-20,15,0});
                    l.rect("fill", {
                        .size  = {200, 130},
                        .color = {1, 1, 1, 1},
                        .pos   = {0, 0, 0},
                        .fill  = Fill::linear({0,0},{1,1},{
                            {0.0f, Color{0.1f, 0.4f, 1.0f, 1}},
                            {1.0f, Color{0.6f, 0.1f, 0.9f, 1}},
                        }),
                    });
                });
                return s.build();
            });

        save(*g_rend.render_frame(comp, 0), "output/new_features/07_gradient_card_3d_tilt.png");
        RenderSettings plain;
        g_rend.set_settings(plain);
    }

    std::printf("\nDone. Open output/new_features/ to view.\n");
    return 0;
}
