#pragma once

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/prepared_text.hpp>

#include <memory>

namespace chronon3d {

class FontEngine;
struct TextRunShape;

// Canonical materializer shared by LayerBuilder and RenderNodeFactory.
// It compiles layout through compile_text_layout(), evaluates the animator
// stack at sample_time, optionally samples AnimatedTextDocument, and returns
// nullptr when shaping/materialization cannot produce a valid run.
[[nodiscard]] std::shared_ptr<TextRunShape> materialize_text_run_shape(
    const TextRunSpec& params,
    FontEngine* engine,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc = nullptr
);

// X2 canonical materializer: consumes a PreparedText directly without
// round-tripping through TextRunSpec / TextSpec.
[[nodiscard]] std::shared_ptr<TextRunShape> materialize_prepared_text(
    const PreparedText& prepared,
    FontEngine* engine,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc = nullptr
);

} // namespace chronon3d
