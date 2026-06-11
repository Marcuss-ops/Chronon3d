#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {

Composition minimalist_clean_quote() {
    return composition({.name = "MinimalistCleanQuote", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);

        // Backdrop layer (no glow)
        s.layer("quote_backdrop", [](auto& l) {
            l.pin_to(Anchor::Center);
            add_text_backdrop(l);
        });

        // Multi-layer cinematic glow — debug mode to find rectangular artifact source
        s.layer("quote_text", [frame = ctx.frame](auto& l) {
            l.pin_to(Anchor::Center);
            auto glow_spec = TextGlowPresets::ae_cinematic_white();
            glow_spec.inner_intensity = 0.18f;
            glow_spec.mid_intensity   = 0.07f;
            glow_spec.bloom_intensity = 0.03f;
            glow_spec.micro_shadow    = false;  // disable to isolate glow issue
            l.glow(glow_spec.to_glow_params());
            auto tp = make_text_params(reveal_text_by_frame(
                "Minimalism is not emptiness.\nIt is the discipline of keeping\nonly what matters.", frame));
            tp.align = TextAlign::Center;  // override theme default
            l.text("quote_text", std::move(tp));
        });

        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registration handled by MinimalistModule in minimalist_module.cpp


