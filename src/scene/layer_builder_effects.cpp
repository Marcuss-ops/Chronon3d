#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <utility>

namespace chronon3d {

LayerBuilder& LayerBuilder::blur(f32 radius) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::BlurGaussian}}, BlurParams{radius}});
    return *this;
}

LayerBuilder& LayerBuilder::tint(Color color, f32 amount) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::ColorTint}}, TintParams{color, amount}});
    return *this;
}

LayerBuilder& LayerBuilder::brightness(f32 v) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::ColorBrightness}}, BrightnessParams{v}});
    return *this;
}

LayerBuilder& LayerBuilder::contrast(f32 v) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::ColorContrast}}, ContrastParams{v}});
    return *this;
}

LayerBuilder& LayerBuilder::saturation(f32 v) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::ColorSaturation}}, SaturationParams{v}});
    return *this;
}

LayerBuilder& LayerBuilder::hue_rotate(f32 deg) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::ColorHueRotate}}, HueRotateParams{deg}});
    return *this;
}

LayerBuilder& LayerBuilder::invert(f32 amount) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::ColorInvert}}, InvertParams{amount}});
    return *this;
}

LayerBuilder& LayerBuilder::vignette(f32 radius, f32 softness, f32 amount) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::ColorVignette}}, VignetteParams{radius, softness, amount}});
    return *this;
}

LayerBuilder& LayerBuilder::drop_shadow(Vec2 offset, Color color, f32 radius) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::LightDropShadow}}, DropShadowParams{offset, color, radius}});
    return *this;
}

LayerBuilder& LayerBuilder::glow(f32 radius, f32 intensity, Color color, f32 threshold, f32 spread, f32 softness) {
    return glow(GlowParams{
        .radius = radius,
        .intensity = intensity,
        .color = color,
        .threshold = threshold,
        .spread = spread,
        .softness = softness
    });
}

LayerBuilder& LayerBuilder::glow(GlowParams params) {
    m_layer.effects.push_back(EffectInstance{
        effects::EffectDescriptor{.id = std::string{effects::ids::LightGlow}},
        std::move(params)
    });
    return *this;
}

LayerBuilder& LayerBuilder::bloom(f32 threshold, f32 radius, f32 intensity) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::LightBloom}}, BloomParams{threshold, radius, intensity}});
    return *this;
}

LayerBuilder& LayerBuilder::fake_3d_wave(Fake3DWaveParams params) {
    m_layer.effects.push_back(EffectInstance{effects::EffectDescriptor{.id = std::string{effects::ids::DistortFake3DWave}}, std::move(params)});
    return *this;
}

LayerBuilder& LayerBuilder::with_shadow(DropShadow shadow) {
    layer_builder_internal::set_last_shadow(m_layer, shadow);
    return *this;
}

LayerBuilder& LayerBuilder::with_glow(Glow glow) {
    layer_builder_internal::set_last_glow(m_layer, glow);
    return *this;
}

LayerBuilder& LayerBuilder::accepts_lights(bool value) {
    m_layer.material.accepts_lights = value;
    return *this;
}

LayerBuilder& LayerBuilder::casts_shadows(bool value) {
    m_layer.material.casts_shadows = value;
    return *this;
}

LayerBuilder& LayerBuilder::accepts_shadows(bool value) {
    m_layer.material.accepts_shadows = value;
    return *this;
}

LayerBuilder& LayerBuilder::material(Material2_5D value) {
    m_layer.material = value;
    return *this;
}

LayerBuilder& LayerBuilder::card3d_material(Card3DMaterial value) {
    m_layer.card3d_material = value;
    return *this;
}

} // namespace chronon3d
