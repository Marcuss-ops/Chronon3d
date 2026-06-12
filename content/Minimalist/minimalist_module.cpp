#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::minimalist {

// New text animations
Composition minimalist_text_fade();
Composition minimalist_text_rise();
Composition minimalist_text_focus();
Composition minimalist_text_scale();
Composition minimalist_text_drift();

} // namespace chronon3d::content::minimalist

namespace chronon3d {

class MinimalistModule : public ExtensionModule {
public:
    std::string_view id() const override { return "minimalist"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::minimalist;

        // First 5 minimalist text animations
        registry.register_composition("MinimalistTextFade", minimalist_text_fade);
        registry.register_composition("MinimalistTextRise", minimalist_text_rise);
        registry.register_composition("MinimalistTextFocus", minimalist_text_focus);
        registry.register_composition("MinimalistTextScale", minimalist_text_scale);
        registry.register_composition("MinimalistTextDrift", minimalist_text_drift);
    }
};

std::unique_ptr<ExtensionModule> create_minimalist_module() {
    return std::make_unique<MinimalistModule>();
}

} // namespace chronon3d
