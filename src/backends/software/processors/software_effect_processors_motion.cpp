#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include "../effects/render_effects_processor.hpp"
#include "effects/color/exposure_levels.hpp"
#include "effects/generate/fill_noise_offset.hpp"
#include "effects/generate/fractal_noise.hpp"
#include "effects/generate/checkerboard.hpp"
#include "effects/generate/four_color_gradient.hpp"
#include "effects/generate/light_rays.hpp"
#include "effects/blur/directional_blur.hpp"
#include "effects/blur/radial_blur.hpp"
#include "effects/stroke/stroke.hpp"
#include "effects/distort/warp.hpp"
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace chronon3d::renderer {

class SoftwareFake3DWaveEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<Fake3DWaveParams>(&params)) {
            apply_fake_3d_wave(fb, *p, context.time_seconds);
        }
    }
};

class SoftwareRadialBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<RadialBlurParams>(&params)) {
            apply_radial_blur(fb, p->center.x, p->center.y,
                              p->amount, p->render_samples,
                              *context.effect_scratch);
        }
    }
};

class SoftwareDirectionalBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<DirectionalBlurParams>(&params)) {
            apply_directional_blur(fb, p->angle, p->length,
                                   *context.effect_scratch, p->samples);
        }
    }
};

class SoftwareOffsetEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<OffsetParams>(&params)) {
            apply_offset(fb, p->offset.x, p->offset.y,
                         p->edge_mode, p->filter,
                         *context.effect_scratch, context.clip);
        }
    }
};

class SoftwareCornerPinEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<CornerPinParams>(&params);
        if (!p) throw std::runtime_error("CornerPin processor received invalid parameters");
        apply_corner_pin(fb, *p, *context.effect_scratch, context.clip);
    }
};

class SoftwareTwirlEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<TwirlParams>(&params);
        if (!p) throw std::runtime_error("Twirl processor received invalid parameters");
        apply_twirl(fb, *p, *context.effect_scratch, context.clip);
    }
};

class SoftwareBulgeEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<BulgeParams>(&params);
        if (!p) throw std::runtime_error("Bulge processor received invalid parameters");
        apply_bulge(fb, *p, *context.effect_scratch, context.clip);
    }
};

class SoftwareMeshWarpEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<MeshWarpParams>(&params);
        if (!p) throw std::runtime_error("MeshWarp processor received invalid parameters");
        apply_mesh_warp(fb, *p, *context.effect_scratch, context.clip);
    }
};

class SoftwareTurbulentDisplaceEffectProcessor final : public EffectProcessor { public:
    void apply(Framebuffer& fb,const EffectParams& params,const effects::EffectExecutionContext& ctx) override {
        const auto* p=std::get_if<TurbulentDisplaceParams>(&params); if(!p) throw std::runtime_error("TurbulentDisplace processor received invalid parameters");
        apply_turbulent_displace(fb,*p,ctx.time_seconds,*ctx.effect_scratch,ctx.clip);
    }
};

class SoftwareWaveWarpEffectProcessor final : public EffectProcessor { public:
    void apply(Framebuffer& fb,const EffectParams& params,const effects::EffectExecutionContext& ctx) override {
        const auto* p=std::get_if<WaveWarpParams>(&params); if(!p) throw std::runtime_error("WaveWarp processor received invalid parameters");
        apply_wave_warp(fb,*p,ctx.time_seconds,*ctx.effect_scratch,ctx.clip);
    }
};

class SoftwareRippleEffectProcessor final : public EffectProcessor { public:
    void apply(Framebuffer& fb,const EffectParams& params,const effects::EffectExecutionContext& ctx) override {
        const auto* p=std::get_if<RippleParams>(&params); if(!p) throw std::runtime_error("Ripple processor received invalid parameters");
        apply_ripple(fb,*p,ctx.time_seconds,*ctx.effect_scratch,ctx.clip);
    }
};

class SoftwareDisplacementMapEffectProcessor final : public EffectProcessor { public:
    void apply(Framebuffer& fb,const EffectParams& params,const effects::EffectExecutionContext& ctx) override {
        const auto* p=std::get_if<DisplacementMapParams>(&params);
        if(!p) throw std::runtime_error("DisplacementMap processor received invalid parameters");
        if (p->field) {
            apply_displacement_field(fb,*p,*p->field,*ctx.effect_scratch,ctx.clip);
            return;
        }
        if(!ctx.second_input) throw std::runtime_error("DisplacementMap requires a resolved map-layer framebuffer");
        apply_displacement_map(fb,*p,*ctx.second_input,*ctx.effect_scratch,ctx.clip);
    }
};

std::unique_ptr<EffectProcessor> create_fake_3d_wave_effect_processor() {
    return std::make_unique<SoftwareFake3DWaveEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_corner_pin_effect_processor() {
    return std::make_unique<SoftwareCornerPinEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_twirl_effect_processor() {
    return std::make_unique<SoftwareTwirlEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_bulge_effect_processor() {
    return std::make_unique<SoftwareBulgeEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_mesh_warp_effect_processor() {
    return std::make_unique<SoftwareMeshWarpEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_turbulent_displace_effect_processor() { return std::make_unique<SoftwareTurbulentDisplaceEffectProcessor>(); }

std::unique_ptr<EffectProcessor> create_wave_warp_effect_processor() { return std::make_unique<SoftwareWaveWarpEffectProcessor>(); }

std::unique_ptr<EffectProcessor> create_ripple_effect_processor() { return std::make_unique<SoftwareRippleEffectProcessor>(); }

std::unique_ptr<EffectProcessor> create_displacement_map_effect_processor() { return std::make_unique<SoftwareDisplacementMapEffectProcessor>(); }

std::unique_ptr<EffectProcessor> create_offset_effect_processor() {
    return std::make_unique<SoftwareOffsetEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_radial_blur_effect_processor() {
    return std::make_unique<SoftwareRadialBlurEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_directional_blur_effect_processor() {
    return std::make_unique<SoftwareDirectionalBlurEffectProcessor>();
}

class SoftwareLensDistortionEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<LensDistortionParams>(&params);
        if (!p) throw std::runtime_error("LensDistortion processor received invalid parameters");
        apply_lens_distortion(fb, *p, *context.effect_scratch, context.clip);
    }
};

class SoftwareFoldWarpEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<FoldWarpParams>(&params);
        if (!p) throw std::runtime_error("FoldWarp processor received invalid parameters");
        apply_fold_warp(fb, *p, *context.effect_scratch, context.clip);
    }
};

class SoftwareCreaseShadingEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<CreaseShadingParams>(&params);
        if (!p) throw std::runtime_error("CreaseShading processor received invalid parameters");
        apply_crease_shading(fb, *p, context.clip);
    }
};

class SoftwareLuminanceResponseEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<LuminanceResponseParams>(&params);
        if (!p) throw std::runtime_error("LuminanceResponse processor received invalid parameters");
        apply_luminance_response(fb, *p, *context.effect_scratch, context.clip);
    }
};

std::unique_ptr<EffectProcessor> create_lens_distortion_effect_processor() {
    return std::make_unique<SoftwareLensDistortionEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_fold_warp_effect_processor() {
    return std::make_unique<SoftwareFoldWarpEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_crease_shading_effect_processor() {
    return std::make_unique<SoftwareCreaseShadingEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_luminance_response_effect_processor() {
    return std::make_unique<SoftwareLuminanceResponseEffectProcessor>();
}

} // namespace chronon3d::renderer
