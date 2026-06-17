#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include "special_names_animations.hpp"

namespace chronon3d {

class SpecialNamesModule : public ExtensionModule {
public:
    std::string_view id() const override { return "special_names"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::special_names;
        registry.register_composition("SpecialNameFadeUp",    special_name_fade_up);
        registry.register_composition("SpecialNameSlideLeft", special_name_slide_left);
        registry.register_composition("SpecialNameSlideRight",special_name_slide_right);
        registry.register_composition("SpecialNameScaleIn",   special_name_scale_in);
        registry.register_composition("SpecialNameStamp",     special_name_stamp);
        registry.register_composition("SpecialNameBlurIn",    special_name_blur_in);
        registry.register_composition("SpecialNameTypewriter", special_name_typewriter);
        registry.register_composition("SpecialNameSplit",     special_name_split);
    }
};

std::unique_ptr<ExtensionModule> create_special_names_module() {
    return std::make_unique<SpecialNamesModule>();
}

} // namespace chronon3d
