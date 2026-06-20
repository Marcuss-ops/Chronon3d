// test_layer_kind.cpp — P2 LayerKind enum + predicate coverage.
//
// All 10 enum values are constructed and asserted against their predicate.
// The default (Layer::kind() with no setter) must remain LayerKind::Normal
// so existing code paths aren't perturbed.

#include <doctest/doctest.h>

#include <chronon3d/scene/model/layer/layer.hpp>

using chronon3d::Layer;
using chronon3d::LayerKind;

TEST_CASE("LayerKind: default construction leaves kind = Normal") {
    Layer l;
    CHECK(l.kind == LayerKind::Normal);
    CHECK_FALSE(l.is_shape());
    CHECK_FALSE(l.is_text());
    CHECK_FALSE(l.is_camera());
    CHECK_FALSE(l.is_audio());
}

TEST_CASE("LayerKind: Shape predicate toggles on LayerKind::Shape") {
    Layer l;
    l.kind = LayerKind::Shape;
    CHECK(l.is_shape());
    CHECK_FALSE(l.is_text());
    CHECK_FALSE(l.is_camera());
    CHECK_FALSE(l.is_audio());
}

TEST_CASE("LayerKind: Text predicate toggles on LayerKind::Text") {
    Layer l;
    l.kind = LayerKind::Text;
    CHECK(l.is_text());
    CHECK_FALSE(l.is_shape());
    CHECK_FALSE(l.is_camera());
    CHECK_FALSE(l.is_audio());
}

TEST_CASE("LayerKind: Camera predicate toggles on LayerKind::Camera") {
    Layer l;
    l.kind = LayerKind::Camera;
    CHECK(l.is_camera());
    CHECK_FALSE(l.is_shape());
    CHECK_FALSE(l.is_text());
    CHECK_FALSE(l.is_audio());
}

TEST_CASE("LayerKind: Audio predicate toggles on LayerKind::Audio") {
    Layer l;
    l.kind = LayerKind::Audio;
    CHECK(l.is_audio());
    CHECK_FALSE(l.is_shape());
    CHECK_FALSE(l.is_text());
    CHECK_FALSE(l.is_camera());
}

TEST_CASE("LayerKind: legacy kinds (Normal/Adjustment/Null/Precomp/Video/Glass) "
          "DO NOT trip the primitives") {
    for (LayerKind k : {LayerKind::Normal, LayerKind::Adjustment,
                        LayerKind::Null, LayerKind::Precomp,
                        LayerKind::Video, LayerKind::Glass}) {
        Layer l;
        l.kind = k;
        CHECK_FALSE(l.is_shape());
        CHECK_FALSE(l.is_text());
        CHECK_FALSE(l.is_camera());
        CHECK_FALSE(l.is_audio());
    }
}

TEST_CASE("LayerKind: enum values are distinct (compile-time)") {
    static_assert(LayerKind::Shape != LayerKind::Text, "distinct enum values");
    static_assert(LayerKind::Camera != LayerKind::Audio, "distinct enum values");
    static_assert(LayerKind::Shape != LayerKind::Camera, "distinct enum values");
    CHECK(true);
}
