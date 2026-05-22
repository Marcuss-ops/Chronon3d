#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/software_effect_runner.hpp>
#include "../utils/render_effects_processor.hpp"

namespace chronon3d::renderer {

class SoftwareBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const std::any& params, float /*time_seconds*/) override {
        if (auto* p = std::any_cast<BlurParams>(&params)) {
            SoftwareEffectRunner::apply_blur(fb, p->radius);
        }
    }
};

class SoftwareTintEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const std::any& params, float /*time_seconds*/) override {
        if (auto* p = std::any_cast<TintParams>(&params)) {
            LayerEffect e;
            e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
            apply_color_effects(fb, e);
        }
    }
};

class SoftwareFake3DWaveEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const std::any& params, float time_seconds) override {
        if (auto* p = std::any_cast<Fake3DWaveParams>(&params)) {
            apply_fake_3d_wave(fb, *p, time_seconds);
        }
    }
};

std::unique_ptr<EffectProcessor> create_blur_effect_processor() {
    return std::make_unique<SoftwareBlurEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_tint_effect_processor() {
    return std::make_unique<SoftwareTintEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_fake_3d_wave_effect_processor() {
    return std::make_unique<SoftwareFake3DWaveEffectProcessor>();
}

} // namespace chronon3d::renderer
