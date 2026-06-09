#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::text {

// ── Forward declarations (definitions in separate .cpp files) ──────────

Composition text_grid_background();
Composition text_basic();
Composition text_hello();
Composition text_image_on_grid();
Composition text_quote_on_grid();
Composition text_shape_on_grid();
Composition text_typewriter();
Composition text_sweep_reveal();
Composition text_stagger_reveal();
Composition text_glow_reveal();

} // namespace chronon3d::content::text

namespace chronon3d {

class TextModule : public ExtensionModule {
public:
    std::string_view id() const override { return "text"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::text;

        // Simple scenes (grid background + clean text)
        registry.register_composition("TextGridBackground", text_grid_background);
        registry.register_composition("TextBasic", text_basic);
        registry.register_composition("TextHello", text_hello);
        registry.register_composition("ImageOnGrid", text_image_on_grid);
        registry.register_composition("QuoteOnGrid", text_quote_on_grid);
        registry.register_composition("ShapeOnGrid", text_shape_on_grid);

        // Typewriter
        registry.register_composition("TextTypewriter", text_typewriter);
        registry.register_composition("TextSweepReveal", text_sweep_reveal);
        registry.register_composition("TextStaggerReveal", text_stagger_reveal);
        registry.register_composition("TextGlowReveal", text_glow_reveal);
    }
};

std::unique_ptr<ExtensionModule> create_text_module() {
    return std::make_unique<TextModule>();
}

} // namespace chronon3d
