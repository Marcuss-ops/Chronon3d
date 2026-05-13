#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/mask_utils.hpp>
#include <chronon3d/scene/layer_effect.hpp>
#include <chronon3d/scene/effect_stack.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <hwy/highway.h>
#include <cmath>
#include <algorithm>
#include <memory_resource>
#include <fmt/format.h>
#include <optional>
#include <cstdint>
#include <type_traits>

namespace hn = hwy::HWY_NAMESPACE;

namespace chronon3d {

namespace {

raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread = 0.0f) {
    Vec2 size{0, 0};
    switch (shape.type) {
        case ShapeType::Rect: size = shape.rect.size; break;
        case ShapeType::RoundedRect: size = shape.rounded_rect.size; break;
        case ShapeType::Circle: size = Vec2{shape.circle.radius * 2, shape.circle.radius * 2}; break;
        case ShapeType::Image: size = shape.image.size; break;
        default: break;
    }

    if (size.x == 0 && size.y == 0) return {0, 0, 0, 0};

    // Expand size by spread
    Vec2 min_local{-spread, -spread};
    Vec2 max_local{size.x + spread, size.y + spread};

    Vec4 corners[4] = {
        model * Vec4(min_local.x, min_local.y, 0, 1),
        model * Vec4(max_local.x, min_local.y, 0, 1),
        model * Vec4(max_local.x, max_local.y, 0, 1),
        model * Vec4(min_local.x, max_local.y, 0, 1)
    };

    f32 min_x = corners[0].x, max_x = corners[0].x;
    f32 min_y = corners[0].y, max_y = corners[0].y;

    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x);
        max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y);
        max_y = std::max(max_y, corners[i].y);
    }

    return {
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)) + 1,
        static_cast<i32>(std::ceil(max_y)) + 1
    };
}

bool hit_test(const Shape& s, Vec2 p, f32 spread = 0.0f) {
    // Note: p is in local space [0, size]
    switch (s.type) {
        case ShapeType::Rect: {
            f32 hw = s.rect.size.x * 0.5f + spread;
            f32 hh = s.rect.size.y * 0.5f + spread;
            f32 px = p.x - s.rect.size.x * 0.5f;
            f32 py = p.y - s.rect.size.y * 0.5f;
            return px >= -hw && px < hw && py >= -hh && py < hh;
        }
        case ShapeType::RoundedRect: {
            f32 hw = s.rounded_rect.size.x * 0.5f + spread;
            f32 hh = s.rounded_rect.size.y * 0.5f + spread;
            f32 px = p.x - s.rounded_rect.size.x * 0.5f;
            f32 py = p.y - s.rounded_rect.size.y * 0.5f;
            f32 r = std::min(s.rounded_rect.radius + spread, std::min(hw, hh));
            // For rounded corners, we use <= for the circle arc but < for the body
            if (std::abs(px) < (hw - r) || std::abs(py) < (hh - r)) {
                return std::abs(px) < hw && std::abs(py) < hh;
            }
            f32 qx = std::abs(px) - (hw - r);
            f32 qy = std::abs(py) - (hh - r);
            return qx * qx + qy * qy < r * r; // Use < to be safe
        }
        case ShapeType::Circle:
            return raster::point_in_circle(p.x - s.circle.radius, p.y - s.circle.radius, s.circle.radius + spread);
        case ShapeType::Image: {
            f32 hw = s.image.size.x * 0.5f + spread;
            f32 hh = s.image.size.y * 0.5f + spread;
            f32 px = p.x - s.image.size.x * 0.5f;
            f32 py = p.y - s.image.size.y * 0.5f;
            return px >= -hw && px < hw && py >= -hh && py < hh;
        }
        default: return false;
    }
}

// Returns false if the screen-space pixel (x, y) falls outside the layer mask.
inline bool pixel_passes_mask(const RenderState& state, i32 x, i32 y) {
    if (!state.mask || !state.mask->enabled()) return true;
    Vec4 local = state.layer_inv_matrix * Vec4(static_cast<f32>(x), static_cast<f32>(y), 0.0f, 1.0f);
    return mask_contains_local_point(*state.mask, Vec2{local.x, local.y});
}

void draw_transformed_shape(Framebuffer& fb, const Shape& shape, const Mat4& model, const Color& color,
                             f32 spread = 0.0f, const RenderState* state = nullptr) {
    if (color.a <= 0.0f) return;

    raster::BBox bbox = compute_world_bbox(shape, model, spread);
    bbox.clip_to(fb.width(), fb.height());

    if (bbox.is_empty()) return;

    Mat4 inv_model = glm::inverse(model);

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;
            Vec4 lp = inv_model * Vec4(static_cast<f32>(x), static_cast<f32>(y), 0.0f, 1.0f);
            if (hit_test(shape, Vec2(lp.x, lp.y), spread)) {
                fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), BlendMode::Normal));
            }
        }
    }
}

} // namespace

namespace {

using rendergraph::RenderCacheKey;
using rendergraph::hash_bytes;
using rendergraph::hash_color;
using rendergraph::hash_combine;
using rendergraph::hash_string;
using rendergraph::hash_transform;
using rendergraph::hash_vec2;
using rendergraph::hash_vec3;

template <typename T>
[[nodiscard]] u64 hash_value_local(const T& value) {
    return rendergraph::hash_bytes(&value, sizeof(T));
}

[[nodiscard]] u64 hash_mask(const Mask& mask) {
    u64 seed = hash_value_local(static_cast<u64>(mask.type));
    seed = hash_combine(seed, hash_vec3(mask.pos));
    seed = hash_combine(seed, hash_vec2(mask.size));
    seed = hash_combine(seed, hash_value_local(mask.radius));
    seed = hash_combine(seed, hash_value_local(mask.inverted));
    return seed;
}

[[nodiscard]] u64 hash_shape(const Shape& shape) {
    u64 seed = hash_value_local(static_cast<u64>(shape.type));
    switch (shape.type) {
        case ShapeType::Rect:
            seed = hash_combine(seed, hash_vec2(shape.rect.size));
            break;
        case ShapeType::RoundedRect:
            seed = hash_combine(seed, hash_vec2(shape.rounded_rect.size));
            seed = hash_combine(seed, hash_value_local(shape.rounded_rect.radius));
            break;
        case ShapeType::Circle:
            seed = hash_combine(seed, hash_value_local(shape.circle.radius));
            break;
        case ShapeType::Line:
            seed = hash_combine(seed, hash_vec3(shape.line.to));
            seed = hash_combine(seed, hash_value_local(shape.line.thickness));
            break;
        case ShapeType::Text:
            seed = hash_combine(seed, hash_string(shape.text.text));
            seed = hash_combine(seed, hash_string(shape.text.style.font_path));
            seed = hash_combine(seed, hash_value_local(shape.text.style.size));
            seed = hash_combine(seed, hash_color(shape.text.style.color));
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(shape.text.style.align)));
            seed = hash_combine(seed, hash_value_local(shape.text.style.line_height));
            seed = hash_combine(seed, hash_value_local(shape.text.style.tracking));
            seed = hash_combine(seed, hash_value_local(shape.text.style.max_lines));
            seed = hash_combine(seed, hash_value_local(shape.text.style.auto_scale));
            seed = hash_combine(seed, hash_value_local(shape.text.style.min_size));
            seed = hash_combine(seed, hash_value_local(shape.text.style.max_size));
            seed = hash_combine(seed, hash_vec2(shape.text.box.size));
            seed = hash_combine(seed, hash_value_local(shape.text.box.enabled));
            break;
        case ShapeType::Image:
            seed = hash_combine(seed, hash_string(shape.image.path));
            seed = hash_combine(seed, hash_vec2(shape.image.size));
            seed = hash_combine(seed, hash_value_local(shape.image.opacity));
            break;
        case ShapeType::Mesh:
            seed = hash_combine(seed, hash_value_local(static_cast<u64>(shape.type)));
            break;
        case ShapeType::None:
        default:
            break;
    }
    return seed;
}

[[nodiscard]] u64 hash_drop_shadow(const DropShadow& shadow) {
    u64 seed = hash_value_local(shadow.enabled);
    seed = hash_combine(seed, hash_vec2(shadow.offset));
    seed = hash_combine(seed, hash_color(shadow.color));
    seed = hash_combine(seed, hash_value_local(shadow.radius));
    return seed;
}

[[nodiscard]] u64 hash_glow(const Glow& glow) {
    u64 seed = hash_value_local(glow.enabled);
    seed = hash_combine(seed, hash_value_local(glow.radius));
    seed = hash_combine(seed, hash_value_local(glow.intensity));
    seed = hash_combine(seed, hash_color(glow.color));
    return seed;
}

[[nodiscard]] u64 hash_node(const RenderNode& node) {
    u64 seed = hash_string(node.name);
    seed = hash_combine(seed, hash_transform(node.world_transform));
    seed = hash_combine(seed, hash_color(node.color));
    seed = hash_combine(seed, hash_shape(node.shape));
    seed = hash_combine(seed, hash_drop_shadow(node.shadow));
    seed = hash_combine(seed, hash_glow(node.glow));
    seed = hash_combine(seed, hash_value_local(node.visible));
    seed = hash_combine(seed, hash_value_local(reinterpret_cast<std::uintptr_t>(node.mesh.get())));
    return seed;
}

[[nodiscard]] u64 hash_effect_stack(const EffectStack& stack) {
    u64 seed = hash_value_local(stack.size());
    for (const auto& inst : stack) {
        seed = hash_combine(seed, hash_value_local(inst.enabled));
        std::visit([&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BlurParams>) {
                seed = hash_combine(seed, hash_value_local(p.radius));
            } else if constexpr (std::is_same_v<T, TintParams>) {
                seed = hash_combine(seed, hash_color(p.color));
                seed = hash_combine(seed, hash_value_local(p.amount));
            } else if constexpr (std::is_same_v<T, BrightnessParams>) {
                seed = hash_combine(seed, hash_value_local(p.value));
            } else if constexpr (std::is_same_v<T, ContrastParams>) {
                seed = hash_combine(seed, hash_value_local(p.value));
            } else if constexpr (std::is_same_v<T, DropShadowParams>) {
                seed = hash_combine(seed, hash_vec2(p.offset));
                seed = hash_combine(seed, hash_color(p.color));
                seed = hash_combine(seed, hash_value_local(p.radius));
            } else if constexpr (std::is_same_v<T, GlowParams>) {
                seed = hash_combine(seed, hash_value_local(p.radius));
                seed = hash_combine(seed, hash_value_local(p.intensity));
                seed = hash_combine(seed, hash_color(p.color));
            }
        }, inst.params);
    }
    return seed;
}

[[nodiscard]] u64 hash_mask_layer(const Mask& mask) {
    return hash_mask(mask);
}

[[nodiscard]] u64 hash_layout_rules(const LayoutRules& layout) {
    u64 seed = hash_value_local(layout.enabled);
    seed = hash_combine(seed, hash_value_local(layout.pin.has_value()));
    if (layout.pin.has_value()) {
        seed = hash_combine(seed, hash_value_local(static_cast<u64>(*layout.pin)));
    }
    seed = hash_combine(seed, hash_value_local(layout.margin));
    seed = hash_combine(seed, hash_value_local(layout.keep_in_safe_area));
    seed = hash_combine(seed, hash_value_local(layout.safe_area.top));
    seed = hash_combine(seed, hash_value_local(layout.safe_area.bottom));
    seed = hash_combine(seed, hash_value_local(layout.safe_area.left));
    seed = hash_combine(seed, hash_value_local(layout.safe_area.right));
    seed = hash_combine(seed, hash_value_local(layout.fit_text));
    return seed;
}

[[nodiscard]] u64 hash_camera_2_5d(const Camera2_5D& camera) {
    u64 seed = hash_value_local(camera.enabled);
    seed = hash_combine(seed, hash_vec3(camera.position));
    seed = hash_combine(seed, hash_vec3(camera.point_of_interest));
    seed = hash_combine(seed, hash_value_local(camera.zoom));
    seed = hash_combine(seed, hash_value_local(camera.dof.enabled));
    seed = hash_combine(seed, hash_value_local(camera.dof.focus_z));
    seed = hash_combine(seed, hash_value_local(camera.dof.aperture));
    seed = hash_combine(seed, hash_value_local(camera.dof.max_blur));
    return seed;
}

[[nodiscard]] u64 hash_camera(const Camera& camera) {
    u64 seed = hash_transform(camera.transform);
    seed = hash_combine(seed, hash_value_local(camera.fov_deg));
    seed = hash_combine(seed, hash_value_local(camera.near_plane));
    seed = hash_combine(seed, hash_value_local(camera.far_plane));
    return seed;
}

[[nodiscard]] u64 hash_layer(const Layer& layer) {
    u64 seed = hash_string(layer.name);
    seed = hash_combine(seed, hash_value_local(static_cast<u64>(layer.kind)));
    seed = hash_combine(seed, hash_transform(layer.transform));
    seed = hash_combine(seed, hash_value_local(layer.from));
    seed = hash_combine(seed, hash_value_local(layer.duration));
    seed = hash_combine(seed, hash_value_local(layer.visible));
    seed = hash_combine(seed, hash_value_local(layer.is_3d));
    seed = hash_combine(seed, hash_mask_layer(layer.mask));
    seed = hash_combine(seed, hash_effect_stack(layer.effects));
    seed = hash_combine(seed, hash_value_local(static_cast<u64>(layer.blend_mode)));
    seed = hash_combine(seed, hash_value_local(static_cast<u64>(layer.depth_role)));
    seed = hash_combine(seed, hash_value_local(layer.depth_offset));
    seed = hash_combine(seed, hash_layout_rules(layer.layout));
    seed = hash_combine(seed, hash_value_local(layer.nodes.size()));
    for (const auto& node : layer.nodes) {
        seed = hash_combine(seed, hash_node(node));
    }
    return seed;
}

[[nodiscard]] RenderCacheKey make_key(std::string scope, Frame frame, i32 width, i32 height,
                                     u64 params_hash, u64 source_hash = 0, u64 input_hash = 0) {
    return RenderCacheKey{
        .scope = std::move(scope),
        .frame = frame,
        .width = width,
        .height = height,
        .params_hash = params_hash,
        .source_hash = source_hash,
        .input_hash = input_hash,
    };
}

} // namespace

// ---------------------------------------------------------------------------
// SoftwareRenderer
// ---------------------------------------------------------------------------

std::unique_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp, Frame frame) {
    if (!m_motion_blur.enabled || m_motion_blur.samples <= 1) {
        Scene scene = comp.evaluate(frame);
        return render_scene_internal(scene, comp.camera, comp.width(), comp.height(), frame, 0.0f);
    }

    // Motion blur: accumulate N subframes evenly distributed over the shutter window.
    // shutter_duration is expressed in frames (e.g. 180° shutter → 0.5 frames).
    const int   N       = std::max(2, m_motion_blur.samples);
    const float shutter = m_motion_blur.shutter_angle / 360.0f; // fraction of a frame
    const int   w       = comp.width();
    const int   h       = comp.height();

    // Floating-point accumulator (r,g,b,a per pixel).
    std::vector<float> accum(static_cast<size_t>(w * h * 4), 0.0f);
    const float weight = 1.0f / static_cast<float>(N);

    for (int s = 0; s < N; ++s) {
        const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
        Scene sub_scene = comp.evaluate(frame, t);
        auto sub_fb = render_scene_internal(sub_scene, comp.camera, w, h, frame, t);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                Color c = sub_fb->get_pixel(x, y);
                const size_t idx = static_cast<size_t>((y * w + x) * 4);
                accum[idx + 0] += c.r * weight;
                accum[idx + 1] += c.g * weight;
                accum[idx + 2] += c.b * weight;
                accum[idx + 3] += c.a * weight;
            }
        }
    }

    auto result = std::make_unique<Framebuffer>(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>((y * w + x) * 4);
            result->set_pixel(x, y, Color{accum[idx], accum[idx+1], accum[idx+2], accum[idx+3]});
        }
    }
    return result;
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height)
{
    return render_scene_internal(scene, camera, width, height, 0, 0.0f);
}

std::string SoftwareRenderer::debug_render_graph(const Scene& scene, const Camera& camera,
                                                 i32 width, i32 height, Frame frame,
                                                 f32 frame_time) const
{
    auto graph = build_render_graph(scene, camera, width, height, frame, frame_time);
    return graph.debug_dot();
}




std::unique_ptr<Framebuffer> SoftwareRenderer::render_scene_internal(
    const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame, f32 frame_time)
{
    ZoneScoped;
    auto graph_wrapper = build_render_graph(scene, camera, width, height, frame, frame_time);

    rendergraph::RenderGraphExecutionContext ctx{
        .renderer = *this,
        .camera = camera,
        .frame = frame,
        .width = width,
        .height = height,
        .diagnostic = diagnostic_,
    };

    return graph_wrapper.execute(ctx);
}


rendergraph::RenderGraph SoftwareRenderer::build_render_graph(
    const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame, f32 frame_time) const
{
    using namespace rendergraph;

    RenderGraph graph;
    const u64 frame_time_hash = hash_value_local(frame_time);
    u64 scene_hash = hash_value_local(scene.nodes().size());
    for (const auto& node : scene.nodes()) {
        scene_hash = hash_combine(scene_hash, hash_node(node));
    }
    for (const auto& layer : scene.layers()) {
        scene_hash = hash_combine(scene_hash, hash_layer(layer));
    }
    scene_hash = hash_combine(scene_hash, hash_camera(camera));
    scene_hash = hash_combine(scene_hash, hash_camera_2_5d(scene.camera_2_5d()));

    RenderCacheKey canvas_key = make_key("canvas", frame, width, height,
                                         hash_combine(scene_hash, frame_time_hash));
    NodeId current = graph.add_output("canvas", canvas_key);
    u64 current_hash = canvas_key.digest();

    auto append_root_source = [&](const ::chronon3d::RenderNode& node, NodeId input) {
        const u64 node_hash = hash_node(node);
        const auto transform_key = make_key("root.transform", frame, width, height,
                                            hash_combine(hash_transform(node.world_transform),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            node_hash, current_hash);
        NodeId transformed = graph.add_transform(std::string(node.name) + ".transform",
                                                 transform_key, input, node.world_transform,
                                                 RenderState{Mat4(1.0f), 1.0f});
        current_hash = transform_key.digest();

        const auto source_key = make_key("root.source", frame, width, height,
                                         hash_combine(node_hash, hash_combine(scene_hash, frame_time_hash)),
                                         node_hash, current_hash);
        NodeId source = graph.add_source(std::string(node.name) + ".source",
                                         source_key, transformed, node);
        current_hash = source_key.digest();

        const auto composite_key = make_key("root.composite", frame, width, height,
                                            hash_combine(static_cast<u64>(BlendMode::Normal),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            node_hash, current_hash);
        current = graph.add_composite(std::string(node.name) + ".composite",
                                      composite_key, current, source, BlendMode::Normal);
        current_hash = composite_key.digest();
    };

    auto append_layer_pipeline = [&](const Layer& layer, const Transform& transform,
                                     NodeId input, const std::string& scope_prefix,
                                     std::optional<f32> dof_blur = std::nullopt) {
        const u64 layer_hash = hash_layer(layer);
        const auto transform_key = make_key(scope_prefix + ".transform", frame, width, height,
                                            hash_combine(hash_transform(transform),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            layer_hash, current_hash);
        NodeId transformed = graph.add_transform(scope_prefix + ".transform",
                                                 transform_key, input, transform,
                                                 RenderState{Mat4(1.0f), 1.0f});
        current_hash = transform_key.digest();

        const auto source_key = make_key(scope_prefix + ".source", frame, width, height,
                                         hash_combine(layer_hash, hash_combine(scene_hash, frame_time_hash)),
                                         layer_hash, current_hash);
        NodeId source = graph.add_layer_source(scope_prefix + ".source",
                                               source_key, transformed, layer);
        current_hash = source_key.digest();

        NodeId after_effects = source;
        if (!layer.effects.empty()) {
            const auto effect_key = make_key(scope_prefix + ".effect", frame, width, height,
                                             hash_combine(hash_effect_stack(layer.effects),
                                                          hash_combine(scene_hash, frame_time_hash)),
                                             layer_hash, current_hash);
            after_effects = graph.add_effect(scope_prefix + ".effect",
                                             effect_key, source, layer);
            current_hash = effect_key.digest();
        }

        if (dof_blur.has_value() && *dof_blur > 0.5f) {
            Layer blur_layer = layer;
            blur_layer.effects.clear();
            blur_layer.effects.push_back(EffectInstance{BlurParams{*dof_blur}});

            const auto blur_key = make_key(scope_prefix + ".dof", frame, width, height,
                                           hash_combine(hash_value_local(*dof_blur), hash_combine(scene_hash, frame_time_hash)),
                                           layer_hash, current_hash);
            after_effects = graph.add_effect(scope_prefix + ".dof",
                                             blur_key, after_effects, blur_layer);
            current_hash = blur_key.digest();
        }

        const auto composite_key = make_key(scope_prefix + ".composite", frame, width, height,
                                            hash_combine(static_cast<u64>(layer.blend_mode),
                                                         hash_combine(scene_hash, frame_time_hash)),
                                            layer_hash, current_hash);
        current = graph.add_composite(scope_prefix + ".composite",
                                      composite_key, current, after_effects, layer.blend_mode);
        current_hash = composite_key.digest();
    };

    auto apply_adjustment_layer = [&](const Layer& layer, const std::string& scope_prefix) {
        if (!layer.effects.empty()) {
            const u64 layer_hash = hash_layer(layer);
            const auto effect_key = make_key(scope_prefix + ".adjustment", frame, width, height,
                                             hash_combine(hash_effect_stack(layer.effects),
                                                          hash_combine(scene_hash, frame_time_hash)),
                                             layer_hash, current_hash);
            current = graph.add_effect(scope_prefix + ".adjustment",
                                       effect_key, current, layer);
            current_hash = effect_key.digest();
        }
    };

    for (const auto& node : scene.nodes()) {
        if (!node.visible) continue;
        append_root_source(node, current);
    }

    const auto& cam25 = scene.camera_2_5d();

    struct LayerRenderItem {
        Layer layer;
        Transform projected_transform{};
        f32 depth{0.0f};
        usize insertion_index{0};
    };

    std::vector<LayerRenderItem> three_d_layers;
    three_d_layers.reserve(scene.layers().size());

    usize index = 0;
    for (const auto& layer : scene.layers()) {
        if (!layer.visible) {
            ++index;
            continue;
        }

        if (layer.kind == LayerKind::Adjustment) {
            apply_adjustment_layer(layer, std::string(layer.name));
            ++index;
            continue;
        }

        if (layer.kind == LayerKind::Null) {
            ++index;
            continue;
        }

        if (!cam25.enabled || !layer.is_3d) {
            append_layer_pipeline(layer, layer.transform, current, std::string(layer.name));
        } else {
            auto projected = project_layer_2_5d(
                layer.transform, cam25, static_cast<f32>(width), static_cast<f32>(height));
            if (projected.visible) {
                three_d_layers.push_back({layer, projected.transform, projected.depth, index});
            }
        }
        ++index;
    }

    if (cam25.enabled) {
        std::stable_sort(three_d_layers.begin(), three_d_layers.end(),
            [](const LayerRenderItem& a, const LayerRenderItem& b) {
                return a.depth > b.depth;
            });

        for (const auto& item : three_d_layers) {
            const Layer& layer = item.layer;
            std::optional<f32> dof_blur;
            if (cam25.dof.enabled) {
                const f32 dist = std::abs(layer.transform.position.z - cam25.dof.focus_z);
                const f32 blur = std::min(dist * cam25.dof.aperture, cam25.dof.max_blur);
                if (blur > 0.5f) {
                    dof_blur = blur;
                }
            }
            append_layer_pipeline(layer, item.projected_transform, current, std::string(layer.name), dof_blur);
        }
    }

    return graph;
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state, const Camera& camera, i32 width, i32 height) {
    const Color linear_color = node.color.to_linear();
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    // Effects (Shadow/Glow)
    if (node.shadow.enabled) draw_shadow(fb, node, state);
    if (node.glow.enabled)   draw_glow(fb, node, state);

    if (node.shape.type == ShapeType::Mesh) {
        if (node.mesh) {
            const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
            render_mesh_wireframe(fb, *node.mesh, model, camera.view_matrix(), camera.projection_matrix(aspect), linear_color);
        }
        return;
    }

    if (node.shape.type == ShapeType::Line) {
        Vec4 p0 = model * Vec4(0, 0, 0, 1);
        Vec4 p1 = model * Vec4(node.shape.line.to, 1);
        Color col = linear_color;
        col.a *= opacity;
        draw_line(fb, Vec3(p0.x, p0.y, p0.z), Vec3(p1.x, p1.y, p1.z), col);
        return;
    }

    if (node.shape.type == ShapeType::Text) {
        Transform text_tr;
        text_tr.position = Vec3(model[3]);
        text_tr.opacity  = opacity;
        // Extract uniform scale from model column lengths so 3D perspective scaling applies to font size.
        text_tr.scale.x  = glm::length(Vec3(model[0]));
        text_tr.scale.y  = glm::length(Vec3(model[1]));
        m_text_renderer.draw_text(node.shape.text, text_tr, fb, &state);
        return;
    }

    if (node.shape.type == ShapeType::Image) {
        m_image_renderer.draw_image(node.shape.image, state, fb);
        return;
    }

    // Main shape
    Color fill_color = linear_color;
    fill_color.a *= opacity;
    draw_transformed_shape(fb, node.shape, model, fill_color, 0.0f, &state);

    if (diagnostic_) {
        draw_diagnostic_info(fb, node, state);
    }
}

void SoftwareRenderer::draw_diagnostic_info(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    if (!diagnostic_) return;

    TextStyle debug_style;
    debug_style.font_path = "assets/fonts/Inter-Regular.ttf";
    debug_style.size = 12.0f;
    debug_style.color = Color{1, 0, 0, 0.8f};

    TextShape debug_text;
    debug_text.text = fmt::format("{}: ({:.1f}, {:.1f})", std::string(node.name), state.matrix[3][0], state.matrix[3][1]);
    debug_text.style = debug_style;

    Transform text_tr;
    text_tr.position = Vec3(state.matrix[3]);
    m_text_renderer.draw_text(debug_text, text_tr, fb);
}

void SoftwareRenderer::render_mesh_wireframe(
    Framebuffer& fb, const Mesh& mesh, const Mat4& model,
    const Mat4& view, const Mat4& proj, const Color& color)
{
    const Mat4 mvp = proj * view * model;
    const auto& vertices = mesh.vertices();
    const auto& indices  = mesh.indices();

    std::vector<Vec3> projected(vertices.size());
    for (usize i = 0; i < vertices.size(); ++i) {
        Vec4 p = mvp * Vec4(vertices[i].position, 1.0f);
        if (p.w != 0.0f) { p.x /= p.w; p.y /= p.w; p.z /= p.w; }
        projected[i] = {(p.x + 1.0f) * 0.5f * fb.width(),
                        (1.0f - (p.y + 1.0f) * 0.5f) * fb.height(), p.z};
    }
    for (usize i = 0; i < indices.size(); i += 3) {
        draw_line(fb, projected[indices[i]],   projected[indices[i+1]], color);
        draw_line(fb, projected[indices[i+1]], projected[indices[i+2]], color);
        draw_line(fb, projected[indices[i+2]], projected[indices[i]],   color);
    }
}

void SoftwareRenderer::draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color) {
    ZoneScoped;
    i32 x0 = static_cast<i32>(p1.x), y0 = static_cast<i32>(p1.y);
    i32 x1 = static_cast<i32>(p2.x), y1 = static_cast<i32>(p2.y);
    const i32 dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const i32 dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy, e2;
    while (true) {
        if (x0 >= 0 && x0 < fb.width() && y0 >= 0 && y0 < fb.height())
            fb.set_pixel(x0, y0, compositor::blend(color, fb.get_pixel(x0, y0), BlendMode::Normal));
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void SoftwareRenderer::draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    ZoneScoped;
    if (node.shadow.color.a <= 0.0f) return;

    const Color base = node.shadow.color.to_linear();
    const Mat4& base_model = state.matrix;
    Mat4 shadow_model = math::translate(Vec3(node.shadow.offset.x, node.shadow.offset.y, 0)) * base_model;

    constexpr int LAYERS = 6;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 spread = node.shadow.radius * t;
        const f32 alpha  = base.a * (1.0f - t * t) * state.opacity;
        if (alpha > 0.0f)
            draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, alpha}, spread, &state);
    }
    draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, base.a * 0.7f * state.opacity}, 0.0f, &state);
}

void SoftwareRenderer::draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    ZoneScoped;
    if (node.glow.intensity <= 0.0f || node.glow.color.a <= 0.0f) return;

    const Color base = node.glow.color.to_linear();
    const Mat4& model = state.matrix;

    constexpr int LAYERS = 5;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 expand = node.glow.radius * t;
        const f32 alpha  = base.a * node.glow.intensity * (1.0f - t) * state.opacity;
        if (alpha > 0.0f)
            draw_transformed_shape(fb, node.shape, model, {base.r, base.g, base.b, alpha}, expand, &state);
    }
}

// ---------------------------------------------------------------------------
// Layer-level effects pipeline
// ---------------------------------------------------------------------------

void SoftwareRenderer::render_layer_nodes(Framebuffer& fb, const Layer& layer,
                                          const RenderState& layer_state,
                                          const Camera& camera, i32 width, i32 height) {
    for (const auto& node : layer.nodes) {
        if (!node.visible) continue;
        RenderState node_state = combine(layer_state, node.world_transform);
        draw_node(fb, node, node_state, camera, width, height);
    }
}

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius) {
    const i32 r = std::max(1, static_cast<i32>(std::round(radius)));
    const i32 w = fb.width(), h = fb.height();
    Framebuffer tmp(w, h);
    tmp.clear(Color::transparent());

    // 3-pass separable box blur ≈ Gaussian
    for (int pass = 0; pass < 3; ++pass) {
        // Horizontal
        for (i32 y = 0; y < h; ++y) {
            Color sum{0, 0, 0, 0};
            for (i32 x = -r; x <= r; ++x) {
                Color p = fb.get_pixel(std::clamp(x, 0, w - 1), y);
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
            for (i32 x = 0; x < w; ++x) {
                tmp.set_pixel(x, y, {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv});
                Color add = fb.get_pixel(std::min(x + r + 1, w - 1), y);
                Color rem = fb.get_pixel(std::max(x - r,     0),     y);
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
        // Vertical
        for (i32 x = 0; x < w; ++x) {
            Color sum{0, 0, 0, 0};
            for (i32 y = -r; y <= r; ++y) {
                Color p = tmp.get_pixel(x, std::clamp(y, 0, h - 1));
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
            for (i32 y = 0; y < h; ++y) {
                fb.set_pixel(x, y, {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv});
                Color add = tmp.get_pixel(x, std::min(y + r + 1, h - 1));
                Color rem = tmp.get_pixel(x, std::max(y - r,     0));
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
    }
}

void SoftwareRenderer::apply_color_effects(Framebuffer& fb, const LayerEffect& effect) {
    const i32 w = fb.width(), h = fb.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.a <= 0.0f) continue;

            // Brightness + contrast: (c + brightness - 0.5) * contrast + 0.5
            if (effect.brightness != 0.0f || effect.contrast != 1.0f) {
                auto adj = [&](f32 v) {
                    return std::clamp((v + effect.brightness - 0.5f) * effect.contrast + 0.5f, 0.0f, 1.0f);
                };
                c.r = adj(c.r); c.g = adj(c.g); c.b = adj(c.b);
            }
            // Tint overlay
            if (effect.tint.a > 0.0f) {
                const f32 t = effect.tint.a;
                c.r = c.r * (1.0f - t) + effect.tint.r * t;
                c.g = c.g * (1.0f - t) + effect.tint.g * t;
                c.b = c.b * (1.0f - t) + effect.tint.b * t;
            }
            fb.set_pixel(x, y, c);
        }
    }
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack) {
    for (const auto& inst : stack) {
        if (!inst.enabled) continue;
        std::visit([&fb](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BlurParams>) {
                if (p.radius > 0.0f) apply_blur(fb, p.radius);
            } else if constexpr (std::is_same_v<T, TintParams>) {
                LayerEffect e;
                e.tint = Color{p.color.r, p.color.g, p.color.b, p.color.a * p.amount};
                apply_color_effects(fb, e);
            } else if constexpr (std::is_same_v<T, BrightnessParams>) {
                LayerEffect e; e.brightness = p.value;
                apply_color_effects(fb, e);
            } else if constexpr (std::is_same_v<T, ContrastParams>) {
                LayerEffect e; e.contrast = p.value;
                apply_color_effects(fb, e);
            }
            // DropShadow, Glow: applied at node level (RenderNode.shadow/glow)
        }, inst.params);
    }
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode) {
    const i32 w = dst.width(), h = dst.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            const Color s = src.get_pixel(x, y);
            if (s.a <= 0.0f) continue;
            dst.set_pixel(x, y, compositor::blend(s, dst.get_pixel(x, y), mode));
        }
    }
}

} // namespace chronon3d
