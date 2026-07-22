#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_content.hpp — Canonical text content substrate
//
// Moved from <chronon3d/scene/builders/params/text_params.hpp> to the text
// module so the text pipeline no longer depends on scene builder headers.
// ══════════════════════════════════════════════════════════════════════════

#include <memory>
#include <string>

namespace chronon3d {

// Forward declaration — full definition lives in <chronon3d/text/font_engine.hpp>
class PlacedGlyphRun;

struct TextContent {
    std::string value;
    std::shared_ptr<PlacedGlyphRun> pre_shaped;
};

} // namespace chronon3d
