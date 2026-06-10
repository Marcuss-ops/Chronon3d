#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// Forward declarations (definitions in separate .cpp files)
Composition anim_fade_in_text();
Composition anim_slide_text();
Composition anim_scale_text();
Composition anim_typewriter();
Composition catmull_rom_showcase();
Composition dolly_zoom_showcase();
Composition camera_spline_comparison();

} // namespace chronon3d::content::anims

namespace chronon3d {

class AnimsModule : public ExtensionModule {
public:
    std::string_view id() const override { return "anims"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::anims;

        // Basic animation compositions
        registry.register_composition("AnimFadeInText",  anim_fade_in_text);
        registry.register_composition("AnimSlideText",   anim_slide_text);
        registry.register_composition("AnimScaleText",   anim_scale_text);
        registry.register_composition("AnimTypewriter",  anim_typewriter);

        // Cinematic showcase compositions
        registry.register_composition("CatmullRomShowcase",      catmull_rom_showcase);
        registry.register_composition("DollyZoomShowcase",       dolly_zoom_showcase);
        registry.register_composition("CameraSplineComparison",  camera_spline_comparison);
    }
};

std::unique_ptr<ExtensionModule> create_anims_module() {
    return std::make_unique<AnimsModule>();
}

} // namespace chronon3d
