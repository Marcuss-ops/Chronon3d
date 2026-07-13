#include "content/common/text/cinematic_glow.hpp"

namespace chronon3d::content::text_reveal {

// apply_cinematic_glow — implementation (no default arg here, per AGENTS.md
// `### C++ default-arg uniqueness per TU` — the default arg lives ONLY in
// the .hpp declaration).
void apply_cinematic_glow(LayerBuilder& l, const CinematicGlowPreset& opts) {
    auto glow = TextGlowPresets::ae_cinematic_white();
    glow.inner_radius    = opts.inner_radius;
    glow.mid_radius      = opts.mid_radius;
    glow.bloom_radius    = opts.bloom_radius;
    glow.inner_intensity = opts.inner_intensity;
    glow.mid_intensity   = opts.mid_intensity;
    glow.bloom_intensity = opts.bloom_intensity;
    l.glow(glow.to_glow_params());
    if (opts.micro_shadow && glow.micro_shadow) {
        l.drop_shadow(opts.shadow_offset, opts.shadow_color, opts.shadow_blur);
    }
    if (opts.drop_shadow) {
        l.drop_shadow(opts.drop_offset, opts.drop_color, opts.drop_blur);
    }
}

} // namespace chronon3d::content::text_reveal
