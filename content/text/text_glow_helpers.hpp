#pragma once

// ── Cinematic Glow Helpers ──────────────────────────────────────────────────
//
// Canonical glow options and apply function used by cinematic text
// compositions.  Centralised here so every composition family can use
// the same AE-style multi-layer glow without duplicating the struct
// and helper in its own file.
//
// Namespace: chronon3d::content::text::glow
// Same namespace as content/text/text_helpers.hpp.
//
// Example:
//   #include "content/text/text_glow_helpers.hpp"
//
//   s.layer("hero", [](LayerBuilder& l) {
//       l.pin_to(Anchor::Center);
//       text::glow::apply_ae_glow(l, text::glow::AeGlowOptions{
//           .inner_radius     = 5.0f,
//           .bloom_radius     = 36.0f,
//           .inner_intensity  = 0.70f,
//       });
//       l.text("t", centered_text({.text = "TITLE", .font_size = 96}));
//   });
//

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_glow_spec.hpp>

namespace chronon3d::content::text::glow {

// TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX — explicit parent-namespace
// imports for the deeper `text::glow` namespace (qualified lookup must
// descend 4 levels: content::text::glow → content::text → content →
// chronon3d, so an explicit `using` block is preferred over unqualified
// cascading for rot-pattern hygiene).
using chronon3d::f32;
using chronon3d::Vec2;
using chronon3d::Color;
using chronon3d::LayerBuilder;

struct AeGlowOptions {
    f32 inner_radius{4.0f};
    f32 mid_radius{14.0f};
    f32 bloom_radius{34.0f};

    f32 inner_intensity{0.55f};
    f32 mid_intensity{0.22f};
    f32 bloom_intensity{0.08f};

    bool micro_shadow{true};
    Vec2  shadow_offset{0.0f, 4.0f};
    Color shadow_color{0.0f, 0.02f, 0.12f, 0.15f};
    f32   shadow_blur{10.0f};

    bool  drop_shadow{false};
    Vec2  drop_offset{0.0f, 8.0f};
    Color drop_color{0.0f, 0.0f, 0.0f, 0.20f};
    f32   drop_blur{18.0f};

    // ── Advanced glow tuning (matching ae_cinematic_white defaults) ──
    f32 softness{1.05f};
    f32 falloff{0.92f};
    f32 outer_downscale{0.25f};
};

inline void apply_ae_glow(LayerBuilder& l, const AeGlowOptions& options = {}) {
    auto glow = TextGlowPresets::ae_cinematic_white();

    glow.inner_radius    = options.inner_radius;
    glow.mid_radius      = options.mid_radius;
    glow.bloom_radius    = options.bloom_radius;

    glow.inner_intensity = options.inner_intensity;
    glow.mid_intensity   = options.mid_intensity;
    glow.bloom_intensity = options.bloom_intensity;

    glow.softness        = options.softness;
    glow.falloff         = options.falloff;
    glow.outer_downscale = options.outer_downscale;

    l.glow(glow.to_glow_params());

    if (options.micro_shadow && glow.micro_shadow) {
        l.drop_shadow(options.shadow_offset, options.shadow_color, options.shadow_blur);
    }

    if (options.drop_shadow) {
        l.drop_shadow(options.drop_offset, options.drop_color, options.drop_blur);
    }
}

} // namespace chronon3d::content::text::glow
