#include <chronon3d/core/composition/composition_registration.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {

Composition minimalist_phrase_float() {
    return composition({.name = "MinimalistPhraseFloat", .width = 1920, .height = 1080, .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);

        s.layer("phrase_float_layer", [frame = ctx.frame](auto& l) {
            l.pin_to(Anchor::Center);
            l.position({0.0f, 0.0f, 0.0f});
            add_text_backdrop(l);
            l.text("phrase_float", make_text_params(reveal_text_by_frame(
                "Space is not wasted here,\nit is the part that lets\nthe sentence breathe.", frame)));
        });

        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registration handled by MinimalistModule in minimalist_module.cpp


