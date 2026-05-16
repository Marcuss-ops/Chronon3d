#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include "../utils/render_effects_processor.hpp"

namespace chronon3d::renderer {

class SoftwareBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params) override {
        if (auto* p = std::get_if<BlurParams>(&params)) {
            SoftwareRenderer::apply_blur(fb, p->radius);
        }
    }
};

class SoftwareTintEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params) override {
        if (auto* p = std::get_if<TintParams>(&params)) {
            LayerEffect e;
            e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
            apply_color_effects(fb, e);
        }
    }
};

std::unique_ptr<EffectProcessor> create_blur_effect_processor() {
    return std::make_unique<SoftwareBlurEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_tint_effect_processor() {
    return std::make_unique<SoftwareTintEffectProcessor>();
}

} // namespace chronon3d::renderer
