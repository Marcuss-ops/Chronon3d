#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/prepared_text.hpp>

#include <memory>
#include <optional>
#include <string>

namespace chronon3d {

class FontEngine;

// Stable storage entry owned by LayerBuilder. `consumed` is a real lifecycle
// flag: production mutation must flow through text_internal::mark_consumed(),
// which also updates the diagnostic counter. The authoring facade mutates the
// payload before consumption but does not own or commit this entry.
struct PendingTextRun {
    std::string name;
    TextRunSpec params;
    std::optional<PreparedText> prepared;
    FontEngine* font_engine{nullptr};
    bool consumed{false};
    std::shared_ptr<const AnimatedTextDocument> animated_doc{};
};

} // namespace chronon3d
