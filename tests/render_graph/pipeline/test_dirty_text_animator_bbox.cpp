// ═══════════════════════════════════════════════════════════════════════════
// tests/render_graph/pipeline/test_dirty_text_animator_bbox.cpp
//
// Regression lock — "frozen typewriter" in a single NVENC run.
//
// Symptom (observed on the typewriter pack): a text run whose animator stack
// reveals glyphs over time rendered correctly through the raw/readback path
// but stayed at its first frame in the encoded H.264 output, until the
// animator settled and a full-frame conversion finally painted the whole
// phrase.  Chunked renders (16-frame chunks) masked the defect.
//
// Cause: the compiled TextRun shape carries its selectors and property
// keyframes, and the per-glyph state is sampled at draw time, so
// `hash_shape_v2(shape)` — the text fingerprint the dirty tracker compares
// frame over frame — is frame-invariant.  Every animating frame therefore
// looked identical to its predecessor: the layer was never re-marked dirty,
// and the native RGBA→NV12 conversion (which converts ONLY the reported
// dirty regions into the pooled encoder frame) kept the stale pixels of the
// glyph block.  Frame deltas were fine; only the region set was wrong.
//
// Fix under test: `populate_layer_semantic_fingerprints` folds the sampled
// frame into the text fingerprint while
// `SceneHasher::has_live_text_run_animation(layer, frame)` holds, so the
// layer bbox (the full text block) is dirtied on every animating frame and
// stops being dirtied once the stack has settled.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include "../../../src/render_graph/pipeline/dirty/layer_bbox_collector.hpp"
#include "../../../src/render_graph/pipeline/frame_delta_compiler.hpp"

#include <memory>
#include <string>
#include <unordered_map>

using namespace chronon3d;
using chronon3d::graph::SceneHasher;
using chronon3d::graph::detail::FrameDeltaCompiler;
using chronon3d::graph::detail::LayerText;

namespace {

constexpr Frame kRevealStart{10};
constexpr Frame kRevealEnd{42};
constexpr raster::BBox kTextBBox{410, 460, 1510, 620};

SampleTime sample_at(Frame frame) {
    return SampleTime::from_frame_int(frame, FrameRate{24, 1});
}

/// A minimal text run whose opacity animator ramps 1 → 0 between
/// `kRevealStart` and `kRevealEnd` and then holds (loop mode Hold, the
/// default) — the typewriter reveal shape the bug froze on.
std::shared_ptr<TextRunShape> make_animating_run() {
    auto shape = std::make_shared<TextRunShape>();

    TextAnimatorSpec animator;
    animator.id = "typewriter_reveal";

    GlyphSelectorSpec selector;
    selector.id = "all_glyphs";
    selector.shape = TextSelectorShape::Square;
    selector.start = AnimatedValue<f32>{0.0f};
    selector.end = AnimatedValue<f32>{100.0f};
    animator.selectors.push_back(selector);

    OpacityProperty opacity;
    opacity.value.add_keyframe(kRevealStart, 1.0f, EasingCurve{Easing::Linear});
    opacity.value.add_keyframe(kRevealEnd, 0.0f, EasingCurve{Easing::Linear});
    animator.properties.push_back(opacity);

    shape->animators.push_back(animator);
    return shape;
}

Layer make_text_layer(std::shared_ptr<TextRunShape> run) {
    Layer layer;
    layer.name = "phrase";
    layer.kind = LayerKind::Text;
    layer.nodes.emplace_back();
    RenderNode& node = layer.nodes.back();
    node.name = "phrase_run";
    node.shape = Shape{TextRunShapeHandle{std::move(run)}};
    return layer;
}

/// Populates the fingerprints exactly as the dirty tracker does, then adds
/// the bbox/transform the bbox collector would have produced for the text
/// block (unchanged frame over frame: the glyph block does not move as the
/// reveal advances).
LayerBBoxState state_at(const Layer& layer, Frame frame) {
    LayerBBoxState state;
    chronon3d::graph::detail::populate_layer_semantic_fingerprints(
        layer, sample_at(frame), state);
    state.bbox = kTextBBox;
    state.world_matrix = Mat4{1.0f};
    state.visible = true;
    return state;
}

} // namespace

TEST_CASE("dirty tracker: animating text run changes its text fingerprint every frame") {
    const Layer layer = make_text_layer(make_animating_run());

    CHECK(SceneHasher::has_live_text_run_animation(layer, kRevealStart));
    CHECK(SceneHasher::has_live_text_run_animation(layer, Frame{20}));
    // Settling is STRICT (`last_keyframe_time() < frame`), so the keyframe
    // frame itself is still live and the first settled frame is the next one.
    CHECK(SceneHasher::has_live_text_run_animation(layer, kRevealEnd));
    CHECK_FALSE(SceneHasher::has_live_text_run_animation(layer, kRevealEnd + Frame{1}));

    const auto at_10 = state_at(layer, Frame{10});
    const auto at_11 = state_at(layer, Frame{11});
    const auto at_20 = state_at(layer, Frame{20});

    CHECK((at_10.semantic_presence & SemanticText) != 0);
    CHECK(at_10.semantic_fingerprints_valid);
    // The compiled shape is identical across frames (selectors/keyframes are
    // part of it): the fingerprint may only differ because the frame is
    // folded in while the animator is still live.
    CHECK(at_10.text_hash != at_11.text_hash);
    CHECK(at_11.text_hash != at_20.text_hash);
}

TEST_CASE("dirty tracker: a settled text run stops changing its text fingerprint") {
    const Layer layer = make_text_layer(make_animating_run());

    const auto at_60 = state_at(layer, Frame{60});
    const auto at_61 = state_at(layer, Frame{61});

    CHECK_FALSE(SceneHasher::has_live_text_run_animation(layer, Frame{60}));
    CHECK(at_60.text_hash == at_61.text_hash);
}

TEST_CASE("FrameDeltaCompiler: animating text run dirties the whole text bbox") {
    const Layer layer = make_text_layer(make_animating_run());

    const std::unordered_map<std::string, LayerBBoxState> previous{
        {"phrase", state_at(layer, Frame{10})}};
    const std::unordered_map<std::string, LayerBBoxState> current{
        {"phrase", state_at(layer, Frame{11})}};

    const auto delta = FrameDeltaCompiler::compile(
        Frame{11}, current, previous, /*camera_changed=*/false, 1920, 1080);

    REQUIRE(delta.changes.size() == 1);
    CHECK((delta.changes.front().change_mask & LayerText) != 0);
    REQUIRE(delta.dirty_bounds.has_value());
    // The dirty rect must cover the glyph block: converting less is exactly
    // what froze the encoded text while the reveal advanced.
    CHECK(delta.dirty_bounds->x0 <= kTextBBox.x0);
    CHECK(delta.dirty_bounds->y0 <= kTextBBox.y0);
    CHECK(delta.dirty_bounds->x1 >= kTextBBox.x1);
    CHECK(delta.dirty_bounds->y1 >= kTextBBox.y1);
}

TEST_CASE("FrameDeltaCompiler: settled text run reports no change") {
    const Layer layer = make_text_layer(make_animating_run());

    const std::unordered_map<std::string, LayerBBoxState> previous{
        {"phrase", state_at(layer, Frame{60})}};
    const std::unordered_map<std::string, LayerBBoxState> current{
        {"phrase", state_at(layer, Frame{61})}};

    const auto delta = FrameDeltaCompiler::compile(
        Frame{61}, current, previous, /*camera_changed=*/false, 1920, 1080);

    CHECK(delta.changes.empty());
    CHECK_FALSE(delta.scene_changed);
    // No damage: the contract represents "nothing to convert" as an EMPTY
    // rect, not as an absent one.
    REQUIRE(delta.dirty_bounds.has_value());
    CHECK(delta.dirty_bounds->x0 == delta.dirty_bounds->x1);
    CHECK(delta.dirty_bounds->y0 == delta.dirty_bounds->y1);
}
