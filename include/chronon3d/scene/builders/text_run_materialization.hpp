#pragma once

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/text/text_run_layout.hpp>
#include <chronon3d/text/text_layout_spec.hpp>
#include <chronon3d/text/text_placement.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <memory>

namespace chronon3d {

class FontEngine;
struct TextRunShape;

/// Immutable output of text preparation. It owns everything that is stable
/// across frames: the shaped/layout result, fill/stroke material, animator
/// definitions and the canonical layout spec. Per-frame glyph states remain
/// in TextRunShape and are evaluated from this object.
struct PreparedTextRun {
    SharedTextRunLayout layout{};
    TextMaterial material{};
    TextPaint paint{};
    std::vector<TextShadow> shadows{};
    std::vector<TextAnimatorSpec> animators{};
    FontEngine* engine{nullptr};
    TextLayoutSpec layout_spec{};
    TextPlacementKind placement_kind{TextPlacementKind::Absolute};
};

/// Prepare immutable layout/material state once. Returns nullptr when the
/// source cannot be shaped or is empty/whitespace-only.
[[nodiscard]] std::shared_ptr<const PreparedTextRun> prepare_text_run(
    const PreparedText& prepared,
    FontEngine* engine);

/// Materialize only the dynamic per-frame view from an already-prepared run.
[[nodiscard]] std::shared_ptr<TextRunShape> materialize_prepared_text(
    const PreparedTextRun& prepared,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc = nullptr);

// Canonical materializer shared by LayerBuilder and RenderNodeFactory.
// It compiles layout through compile_text_layout(), evaluates the animator
// stack at sample_time, optionally samples AnimatedTextDocument, and returns
// nullptr when shaping/materialization cannot produce a valid run.
[[nodiscard]] std::shared_ptr<TextRunShape> materialize_prepared_text(
    const PreparedText& prepared,
    FontEngine* engine,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc = nullptr
);

} // namespace chronon3d
