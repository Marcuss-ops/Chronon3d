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
};

inline void apply_ae_glow(LayerBuilder& l, const AeGlowOptions& options = {}) {
    auto glow = TextGlowPresets::ae_cinematic_white();

    glow.inner_radius    = options.inner_radius;
    glow.mid_radius      = options.mid_radius;
    glow.bloom_radius    = options.bloom_radius;

    glow.inner_intensity = options.inner_intensity;
    glow.mid_intensity   = options.mid_intensity;
    glow.bloom_intensity = options.bloom_intensity;

    l.glow(glow.to_glow_params());

    if (options.micro_shadow && glow.micro_shadow) {
        l.drop_shadow(options.shadow_offset, options.shadow_color, options.shadow_blur);
    }

    if (options.drop_shadow) {
        l.drop_shadow(options.drop_offset, options.drop_color, options.drop_blur);
    }
}

} // namespace chronon3d::content::text::glow
