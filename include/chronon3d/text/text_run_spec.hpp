#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_run_spec.hpp — Legacy animated text-run specification (transitional)
//
// Moved from <chronon3d/scene/builders/params/text_params.hpp> to the text
// module.  New code should prefer TextDefinition with a non-empty
// TextAnimation.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_direction.hpp>
#include <chronon3d/text/text_spec.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>
#include <chronon3d/text/text_animator_property.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d {

struct TextRunSpec {
    TextSpec text;
    TextDirection direction{TextDirection::Auto};
    std::string language;
    std::uint32_t script{0u};
    std::vector<TextAnimatorSpec> animators;
    std::vector<GlyphSelectorSpec> selectors;
    bool cache_layout{true};
};

} // namespace chronon3d
