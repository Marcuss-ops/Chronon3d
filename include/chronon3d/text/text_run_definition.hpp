#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_run_definition.hpp — Runtime text-run definition.
//
// This payload combines a TextDefaults document substrate with the
// per-glyph animation stack used by the text compiler.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_direction.hpp>
#include <chronon3d/text/text_defaults.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>
#include <chronon3d/text/text_animator_property.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d {

struct TextRunDefinition {
    TextDefaults text;
    TextDirection direction{TextDirection::Auto};
    std::string language;
    std::uint32_t script{0u};
    std::vector<TextAnimatorSpec> animators;
    std::vector<GlyphSelectorSpec> selectors;
    bool cache_layout{true};
};

} // namespace chronon3d
