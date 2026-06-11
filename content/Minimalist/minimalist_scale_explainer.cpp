#include <chronon3d/core/composition/composition_registration.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {

Composition minimalist_scale_explainer() {
    return composition({.name = "MinimalistScaleExplainer", .width = 1920, .height = 1080, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);

        s.layer("hero_title_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.scale_drop(1.06f, Frame{40});
            add_text_backdrop(l, BACKDROP_SIZE_SMALL);
            l.text("hero_title", make_text_params("A minimalist frame does not\nask for attention.\nIt protects what matters.", TEXT_SIZE_SMALL));
        });

        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registration handled by MinimalistModule in minimalist_module.cpp
