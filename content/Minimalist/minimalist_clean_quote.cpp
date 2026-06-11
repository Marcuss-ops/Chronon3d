#include <chronon3d/core/composition/composition_registration.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {

Composition minimalist_clean_quote() {
    return composition({.name = "MinimalistCleanQuote", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);

        s.layer("quote_text_layer", [frame = ctx.frame](auto& l) {
            l.pin_to(Anchor::Center);
            add_text_backdrop(l);
            l.text("quote_text", make_text_params(reveal_text_by_frame(
                "Minimalism is not emptiness.\nIt is the discipline of keeping\nonly what matters.", frame)));
        });

        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registration handled by MinimalistModule in minimalist_module.cpp


