#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/text/text_run_hash.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <xxhash.h>

#include <bit>
#include <type_traits>
#include <string_view>
// <variant> is transitively included via effect_stack.hpp → effect_params.hpp

namespace chronon3d::graph {

// Cat-3 — the digest primitive (mixing formula + XXH64 byte/string
// digests) has ONE seat: core/hash/hash_builder.hpp.  These graph-domain
// names delegate to it so every cache-key digest shares one implementation;
// the formula is unchanged, so digest values are bit-identical to the
// historical inline versions.
[[nodiscard]] u64 hash_combine(u64 seed, u64 value) ;

[[nodiscard]] u64 hash_bytes(const void* data, usize size) ;

[[nodiscard]] u64 hash_string(std::string_view value) ;

template <typename T>
[[nodiscard]] inline u64 hash_value(const T& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] u64 hash_vec2(const Vec2& value) ;

[[nodiscard]] u64 hash_vec3(const Vec3& value) ;

[[nodiscard]] u64 hash_color(const Color& color) ;

[[nodiscard]] u64 hash_transform(const Transform& transform) ;

[[nodiscard]] u64 hash_surface_policy(SurfacePolicy policy) ;

[[nodiscard]] u64 hash_transform_policy(TransformPolicy policy) ;

[[nodiscard]] u64 hash_video_source(const video::VideoSource& source) ;



[[nodiscard]] u64 hash_gradient(const graphics::GradientDefinition& gradient) ;

[[nodiscard]] u64 hash_fill(const Fill& f) ;






[[nodiscard]] u64 hash_glow_params(const GlowParams& p) ;


[[nodiscard]] u64 hash_effect_stack(const EffectStack& effects) ;

[[nodiscard]] u64 hash_mask(const Mask& m) ;


[[nodiscard]] u64 hash_shape(const Shape& s) ;

// ── Text hashing (canonical, used by all text caches) ─────────────


[[nodiscard]] u64 hash_layer_transition_parameters(const LayerTransitionParameters& params) ;

[[nodiscard]] u64 hash_layer_transition_spec(const LayerTransitionSpec& spec) ;

[[nodiscard]] u64 hash_render_node_content_only(const RenderNode& n) ;

[[nodiscard]] u64 hash_render_node_placement_only(const RenderNode& n) ;

[[nodiscard]] u64 hash_render_node(const RenderNode& n) ;

} // namespace chronon3d::graph
