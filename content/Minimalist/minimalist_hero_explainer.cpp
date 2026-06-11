#include <chronon3d/core/composition/composition_registration.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {

Composition minimalist_hero_explainer() {
    return composition({.name = "MinimalistHeroExplainer", .width = 1920, .height = 1080, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);

        s.layer("hero_title_layer", [frame = ctx.frame](auto& l) {
            l.pin_to(Anchor::Center);
            add_text_backdrop(l);
            l.text("hero_title", make_text_params(reveal_text_by_frame(
                "A minimalist frame does not\nask for attention.\nIt protects what matters.", frame)));
        });

        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registration handled by MinimalistModule in minimalist_module.cpp


