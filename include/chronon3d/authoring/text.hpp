#pragma once

// Text is a move-only, non-owning authoring handle over the PendingTextRun
// stored by LayerBuilder. All mutators write directly into that canonical
// pending specification; destruction has no commit side effect.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_direction.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>

#include <chronon3d/authoring/animator.hpp>
#include <chronon3d/authoring/material.hpp>
#include <chronon3d/authoring/style_registry.hpp>
#include <chronon3d/authoring/motion_registry.hpp>
#include <chronon3d/authoring/resolution_outcome.hpp>
#include <chronon3d/authoring/text_span_builder.hpp>
#include <chronon3d/assets/asset_ref.hpp>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::authoring {

class Layer;
namespace testing { class TextRunBuilderInspector; }

class Text {
public:
#include <chronon3d/authoring/detail/text_content_font.hpp>
#include <chronon3d/authoring/detail/text_placement_layout.hpp>
#include <chronon3d/authoring/detail/text_appearance_animation.hpp>
#include <chronon3d/authoring/detail/text_registry_access.hpp>

    friend class TextSpanBuilder;

    /// Start a new rich-text span.  The text is appended to the
    /// underlying content and a TextSpanOverride covering the appended
    /// bytes is created.  Returns a TextSpanBuilder for chaining style
    /// overrides (color, weight, tracking, stroke, ...).
    TextSpanBuilder span(std::string_view text) {
        auto& spans = pending_->params.text.spans;
        const std::size_t start = pending_->params.text.content.value.size();
        pending_->params.text.content.value.append(text);
        const std::size_t end = pending_->params.text.content.value.size();
        spans.push_back(TextSpanOverride{.byte_start = start, .byte_end = end});
        return TextSpanBuilder{*this, spans.size() - 1};
    }

private:
#include <chronon3d/authoring/detail/text_private.hpp>
};

// Inline definition lives here because it needs the complete Text class.
inline TextSpanBuilder TextSpanBuilder::span(std::string_view text) {
    return parent_->span(text);
}

inline TextSpanOverride& TextSpanBuilder::current_span() noexcept {
    return parent_->pending_->params.text.spans[span_index_];
}

} // namespace chronon3d::authoring
