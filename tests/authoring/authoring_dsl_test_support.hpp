#pragma once

#include <doctest/doctest.h>

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/authoring/authoring.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_animator_property.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>

using chronon3d::Color;
using chronon3d::Frame;
using chronon3d::Vec2;
using chronon3d::Vec3;
using chronon3d::Easing;
using chronon3d::EasingCurve;
using chronon3d::SelectorCombineMode;
using chronon3d::SelectorWeight;
using chronon3d::TextMaterial;
using chronon3d::TextPropertyBlendMode;
using chronon3d::TextSelectorOrder;
using chronon3d::TextSelectorShape;
using chronon3d::TextSelectorUnit;
using chronon3d::GlyphSelectorSpec;
using chronon3d::TextAnimatorSpec;

using chronon3d::authoring::Animator;
using chronon3d::authoring::Material;
using chronon3d::authoring::MotionRegistry;
using chronon3d::authoring::Selector;
using chronon3d::authoring::StyleRegistry;

using chronon3d::SceneBuilder;
using chronon3d::FrameRate;
using chronon3d::OpacityProperty;
using chronon3d::BlurProperty;
using chronon3d::TrackingProperty;
using chronon3d::PositionProperty;
using chronon3d::ScaleProperty;
using chronon3d::RotationProperty;
using chronon3d::AnchorProperty;
using chronon3d::FillColorProperty;
using chronon3d::StrokeColorProperty;
using chronon3d::StrokeWidthProperty;
using chronon3d::LayerBuilder;
using chronon3d::PendingTextRun;
using chronon3d::TextAlign;
using chronon3d::TextAnchor;
using chronon3d::TextCenteringMode;
using chronon3d::TextDirection;
using chronon3d::TextOverflow;
using chronon3d::TextStyle;
using chronon3d::TextWrap;
using chronon3d::VerticalAlign;
using chronon3d::f32;
using chronon3d::authoring::animator;
namespace material = chronon3d::authoring::material;
using chronon3d::authoring::selector;

namespace doctest {
struct Approx3D { Vec3 v; explicit Approx3D(Vec3 v_) : v(v_) {} };
struct Approx2D { Vec2 v; explicit Approx2D(Vec2 v_) : v(v_) {} };
} // namespace doctest

inline bool operator==(Vec3 a, const doctest::Approx3D& b) {
    return a.x == doctest::Approx(b.v.x).epsilon(1e-5)
        && a.y == doctest::Approx(b.v.y).epsilon(1e-5)
        && a.z == doctest::Approx(b.v.z).epsilon(1e-5);
}
inline bool operator!=(Vec3 a, const doctest::Approx3D& b) { return !(a == b); }
inline bool operator==(Vec2 a, const doctest::Approx2D& b) {
    return a.x == doctest::Approx(b.v.x).epsilon(1e-5)
        && a.y == doctest::Approx(b.v.y).epsilon(1e-5);
}
inline bool operator!=(Vec2 a, const doctest::Approx2D& b) { return !(a == b); }

namespace chronon3d::authoring::testing {

struct AnimatorTestAccess {
    static TextAnimatorSpec release(Animator&& a) {
        return std::move(a).release();
    }
    static GlyphSelectorSpec release(Selector&& s) {
        return std::move(s).release();
    }
    static TextMaterial release(Material&& m) {
        return std::move(m).release();
    }
};

struct MaterialTestAccess {
    static TextMaterial release(Material&& m) {
        return std::move(m).release();
    }
};

// Authoring-test-only snapshot over the PendingTextRun owned by LayerBuilder.
// The pointer remains non-owning; the owning LayerBuilder must outlive it.
struct TextRunSnapshot {
    std::string name;
    const chronon3d::PendingTextRun* pending;

    [[nodiscard]] const chronon3d::PendingTextRun* operator->() const noexcept {
        return pending;
    }
    [[nodiscard]] const chronon3d::PendingTextRun& operator*() const noexcept {
        return *pending;
    }
};

class TextRunBuilderInspector {
public:
    [[nodiscard]] static TextRunSnapshot pending_of(chronon3d::authoring::Text& text) {
        auto& pending = text.mutable_pending();
        return TextRunSnapshot{std::string(pending.name), &pending};
    }
};

} // namespace chronon3d::authoring::testing

using chronon3d::authoring::testing::AnimatorTestAccess;
using chronon3d::authoring::testing::MaterialTestAccess;

namespace authoring_test_support {

template <class Fn>
void for_each_property(const TextAnimatorSpec& spec, Fn&& fn) {
    for (const auto& p : spec.properties) {
        std::visit(std::forward<Fn>(fn), p);
    }
}

inline bool find_property(const TextAnimatorSpec& spec,
                          const std::type_info& target,
                          std::function<bool(const void*)> match) {
    bool found = false;
    for_each_property(spec, [&](const auto& v) {
        if (found) return;
        if (typeid(v) == target) {
            found = match(static_cast<const void*>(&v));
        }
    });
    return found;
}

} // namespace authoring_test_support

using authoring_test_support::find_property;
