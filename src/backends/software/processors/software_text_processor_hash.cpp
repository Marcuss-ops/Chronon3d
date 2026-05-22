#include "software_text_processor_internal.hpp"

namespace chronon3d::renderer {

using chronon3d::graph::hash_combine;
using chronon3d::graph::hash_value;
using chronon3d::graph::hash_string;

CacheKey hash_text_shape(const TextShape& text, float effective_size) {
    CacheKey seed = 0;
    seed = hash_combine(seed, hash_string(text.text));
    seed = hash_combine(seed, hash_string(text.style.font_path));
    seed = hash_combine(seed, hash_string(text.style.font_family));
    seed = hash_combine(seed, hash_value(text.style.font_weight));
    seed = hash_combine(seed, hash_string(text.style.font_style));
    seed = hash_combine(seed, hash_value(effective_size));
    seed = hash_combine(seed, hash_value(text.style.color.r));
    seed = hash_combine(seed, hash_value(text.style.color.g));
    seed = hash_combine(seed, hash_value(text.style.color.b));
    seed = hash_combine(seed, hash_value(text.style.color.a));
    seed = hash_combine(seed, hash_value(static_cast<int>(text.style.align)));
    seed = hash_combine(seed, hash_value(text.style.line_height));
    seed = hash_combine(seed, hash_value(text.style.tracking));
    seed = hash_combine(seed, hash_value(text.style.max_lines));
    seed = hash_combine(seed, hash_value(text.style.auto_scale));
    seed = hash_combine(seed, hash_value(text.style.min_size));
    seed = hash_combine(seed, hash_value(text.style.max_size));
    seed = hash_combine(seed, hash_value(text.box.size.x));
    seed = hash_combine(seed, hash_value(text.box.size.y));
    seed = hash_combine(seed, hash_value(text.box.enabled));
    return seed;
}

CacheKey hash_glow_params(const RenderNode& node, float effective_size) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(node.glow.radius));
    seed = hash_combine(seed, hash_value(node.glow.intensity));
    seed = hash_combine(seed, hash_value(node.glow.color.r));
    seed = hash_combine(seed, hash_value(node.glow.color.g));
    seed = hash_combine(seed, hash_value(node.glow.color.b));
    seed = hash_combine(seed, hash_value(node.glow.color.a));
    return seed;
}

CacheKey hash_shadow_params(const RenderNode& node, float effective_size, size_t index) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(index));
    const auto& shadow = node.shape.text.style.shadows[index];
    seed = hash_combine(seed, hash_value(shadow.blur));
    seed = hash_combine(seed, hash_value(shadow.opacity));
    seed = hash_combine(seed, hash_value(shadow.color.r));
    seed = hash_combine(seed, hash_value(shadow.color.g));
    seed = hash_combine(seed, hash_value(shadow.color.b));
    seed = hash_combine(seed, hash_value(shadow.color.a));
    return seed;
}

} // namespace chronon3d::renderer
