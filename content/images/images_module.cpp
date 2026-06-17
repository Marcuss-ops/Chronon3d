#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include "compositions/image_test_patterns.hpp"
#include "compositions/image_animated_reveals.hpp"
#include "compositions/image_proofs.hpp"

namespace chronon3d {

class ImagesModule : public ExtensionModule {
public:
    std::string_view id() const override { return "images"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::images;

        // Test pattern / synthetic image compositions
        registry.register_composition("ImgGradient",    img_gradient);
        registry.register_composition("ImgChecker",     img_checker);
        registry.register_composition("ImgGridTest",    img_grid_test);
        registry.register_composition("ImgTestPattern", img_test_pattern);

        // Animated / camera-reveal image compositions
        registry.register_composition("ImgShakeZoom",             img_shake_zoom);
        registry.register_composition("ImgReferenceShakeReveal",  img_reference_shake_reveal);
        registry.register_composition("ImgCornerSmoothing",       img_corner_smoothing);

        // Matrix of image properties (fit modes, focal points, opacity,
        // tint, masks, glow, shadow, blur, fallback, transparency)
        registry.register_composition("ImageProofs", image_proofs);
    }
};

std::unique_ptr<ExtensionModule> create_images_module() {
    return std::make_unique<ImagesModule>();
}

} // namespace chronon3d
