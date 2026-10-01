#pragma once

#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace chronon3d::render_plan {

/// BackdropFilterPlan authority in the compositor.
/// Samples ONLY preceding composited content behind the layer and applies
/// optical transmission, blur, chromatic dispersion, refraction, and rim lighting.
struct BackdropFilterPlan {
    float blur{16.0f};
    float brightness{1.05f};
    float saturation{1.10f};

    float refraction_strength{0.0f};
    std::optional<graphics::Field2D> refraction_field{std::nullopt};

    float rim_strength{0.40f};
    float rim_width{3.0f};

    float chromatic_dispersion{2.0f};
    float grain{0.04f};

    float opacity{1.0f};

    void validate() const {
        if (!std::isfinite(blur) || blur < 0.0f || blur > 256.0f)
            throw std::invalid_argument("BackdropFilter blur must be in [0, 256]");
        if (!std::isfinite(brightness) || brightness < 0.0f || brightness > 8.0f)
            throw std::invalid_argument("BackdropFilter brightness must be in [0, 8]");
        if (!std::isfinite(saturation) || saturation < 0.0f || saturation > 8.0f)
            throw std::invalid_argument("BackdropFilter saturation must be in [0, 8]");
        if (!std::isfinite(refraction_strength) || refraction_strength < 0.0f || refraction_strength > 128.0f)
            throw std::invalid_argument("BackdropFilter refraction_strength must be in [0, 128]");
        if (!std::isfinite(rim_strength) || rim_strength < 0.0f || rim_strength > 4.0f)
            throw std::invalid_argument("BackdropFilter rim_strength must be in [0, 4]");
        if (!std::isfinite(rim_width) || rim_width < 0.0f || rim_width > 64.0f)
            throw std::invalid_argument("BackdropFilter rim_width must be in [0, 64]");
        if (!std::isfinite(chromatic_dispersion) || chromatic_dispersion < 0.0f || chromatic_dispersion > 32.0f)
            throw std::invalid_argument("BackdropFilter chromatic_dispersion must be in [0, 32]");
        if (!std::isfinite(grain) || grain < 0.0f || grain > 1.0f)
            throw std::invalid_argument("BackdropFilter grain must be in [0, 1]");
        if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
            throw std::invalid_argument("BackdropFilter opacity must be in [0, 1]");
    }

    // ── Canonical Glass Presets ──

    [[nodiscard]] static BackdropFilterPlan glass_frosted() noexcept {
        BackdropFilterPlan p;
        p.blur = 28.0f;
        p.brightness = 1.08f;
        p.saturation = 1.15f;
        p.refraction_strength = 2.0f;
        p.rim_strength = 0.45f;
        p.rim_width = 3.5f;
        p.chromatic_dispersion = 1.5f;
        p.grain = 0.05f;
        p.opacity = 1.0f;
        return p;
    }

    [[nodiscard]] static BackdropFilterPlan glass_acrylic() noexcept {
        BackdropFilterPlan p;
        p.blur = 16.0f;
        p.brightness = 1.04f;
        p.saturation = 1.05f;
        p.refraction_strength = 4.0f;
        p.rim_strength = 0.60f;
        p.rim_width = 2.0f;
        p.chromatic_dispersion = 3.0f;
        p.grain = 0.02f;
        p.opacity = 1.0f;
        return p;
    }

    [[nodiscard]] static BackdropFilterPlan glass_thin() noexcept {
        BackdropFilterPlan p;
        p.blur = 8.0f;
        p.brightness = 1.02f;
        p.saturation = 1.02f;
        p.refraction_strength = 1.0f;
        p.rim_strength = 0.30f;
        p.rim_width = 1.5f;
        p.chromatic_dispersion = 0.8f;
        p.grain = 0.01f;
        p.opacity = 1.0f;
        return p;
    }

    [[nodiscard]] static BackdropFilterPlan glass_heavy() noexcept {
        BackdropFilterPlan p;
        p.blur = 48.0f;
        p.brightness = 1.12f;
        p.saturation = 1.25f;
        p.refraction_strength = 6.0f;
        p.rim_strength = 0.75f;
        p.rim_width = 5.0f;
        p.chromatic_dispersion = 4.0f;
        p.grain = 0.08f;
        p.opacity = 1.0f;
        return p;
    }

    [[nodiscard]] static BackdropFilterPlan glass_refractive() noexcept {
        BackdropFilterPlan p;
        p.blur = 12.0f;
        p.brightness = 1.05f;
        p.saturation = 1.10f;
        p.refraction_strength = 12.0f;
        p.rim_strength = 0.80f;
        p.rim_width = 4.0f;
        p.chromatic_dispersion = 6.0f;
        p.grain = 0.03f;
        p.opacity = 1.0f;
        return p;
    }
};

} // namespace chronon3d::render_plan
