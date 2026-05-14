#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

namespace chronon3d::presets {

/**
 * Studio-grade presets for SceneBuilder.
 */
struct Studio {
    static void glass_panel(SceneBuilder& s, std::string name, Vec3 pos, Vec2 size, f32 blur_radius = 20.0f, f32 opacity = 0.5f) {
        s.layer(std::move(name), [pos, size, blur_radius, opacity](LayerBuilder& l) {
            l.kind(LayerKind::Glass)
             .position(pos)
             .rect("glass_base", { .size = size, .color = Color::white() })
             .opacity(opacity)
             .blur(blur_radius);
        });
    }

    static void contact_shadow(SceneBuilder& s, std::string name, ContactShadowParams p) {
        // Core "occlusion" shadow (sharper, darker, smaller)
        s.layer(name + "_core", [p](LayerBuilder& l) {
            l.enable_3d()
             .position(p.pos + Vec3{0, 1.0f, 0})
             .rounded_rect("core", {
                 .size   = p.size * 0.75f,
                 .radius = std::min(p.size.x, p.size.y) * 0.25f,
                 .color  = p.color.with_alpha(p.opacity * 0.9f),
             })
             .blur(p.blur * 0.3f);
        });

        // Soft ambient shadow (larger, softer)
        s.layer(std::move(name), [p](LayerBuilder& l) {
            l.enable_3d()
             .position(p.pos)
             .rounded_rect("soft", {
                 .size   = p.size,
                 .radius = std::min(p.size.x, p.size.y) * 0.35f,
                 .color  = p.color.with_alpha(p.opacity * 0.6f),
             })
             .blur(p.blur);
        });
    }

    static void fake_box3d(SceneBuilder& s, std::string name, FakeBox3DParams p) {
        if (p.contact_shadow) {
            contact_shadow(s, name + "_shadow", {
                .pos    = p.pos,
                .size   = {p.size.x * 1.3f, p.size.y * 1.3f},
                .blur   = 25.0f,
                .opacity = 0.40f
            });
        }
        if (p.reflective) {
            s.layer(name + "_reflection", [p](LayerBuilder& l) {
                auto rp = p;
                const f32 floor_y = p.floor_y; 
                rp.pos.y = floor_y - (p.pos.y - floor_y);
                l.enable_3d().position(rp.pos).fake_box3d("reflection", rp)
                 .opacity(0.35f).blur(12.0f);
            });
        }
        s.layer(std::move(name), [p](LayerBuilder& l) {
            l.enable_3d().position(p.pos).fake_box3d("box", p);
        });
    }

    static void grid_plane(SceneBuilder& s, std::string name, GridPlaneParams p) {
        s.layer(std::move(name), [p](LayerBuilder& l) {
            l.enable_3d().position(p.pos).grid_plane("grid", p);
        });
    }

    static void fake_extruded_text(SceneBuilder& s, std::string name, FakeExtrudedTextParams p) {
        s.layer(std::move(name), [p](LayerBuilder& l) {
            l.enable_3d()
             .position(p.pos)
             .fake_extruded_text("text", p);
        });
    }
};

/**
 * Common effect presets for LayerBuilder.
 */
struct Effects {
    static void bloom_soft(LayerBuilder& l)   { l.bloom(0.85f, 18.0f, 0.35f); }
    static void bloom_medium(LayerBuilder& l) { l.bloom(0.80f, 28.0f, 0.60f); }
    static void bloom_strong(LayerBuilder& l) { l.bloom(0.75f, 42.0f, 0.85f); }
};

} // namespace chronon3d::presets
