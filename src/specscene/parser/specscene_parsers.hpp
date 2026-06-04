#pragma once

#include <chronon3d/specscene/model/specscene.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/depth_role.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/transform.hpp>
#include <toml++/toml.h>
#include <string>
#include <vector>

namespace chronon3d::specscene {

// -----------------------------------------------------------------------
// Template helpers (must be in header)
// -----------------------------------------------------------------------

template <typename T>
T read_scalar(const toml::table& tbl, const char* key, T fallback) {
    return tbl[key].value_or<T>(T{fallback});
}

template <typename T>
AnimatedValue<T> read_animated_scalar(const toml::table& tbl, const char* key, T fallback) {
    const auto node = tbl[key];
    if (const auto* str = node.as_string()) {
        AnimatedValue<T> av(fallback);
        av.expression(str->get());
        return av;
    }
    return AnimatedValue<T>(read_scalar<T>(tbl, key, fallback));
}

// -----------------------------------------------------------------------
// Non-template parser declarations
// -----------------------------------------------------------------------

std::string lower_copy(std::string s);

Vec2 read_vec2(const toml::table& tbl, const char* key, Vec2 fallback);
Vec3 read_vec3(const toml::table& tbl, const char* key, Vec3 fallback);
Color read_color(const toml::table& tbl, const char* key, Color fallback);

BlendMode parse_blend_mode(std::string value, BlendMode fallback = BlendMode::Normal);
DepthRole parse_depth_role(std::string value, DepthRole fallback = DepthRole::None);

void note(std::vector<std::string>* diagnostics, std::string message);

std::optional<VisualDesc> parse_visual(const toml::table& tbl, std::vector<std::string>* diagnostics);
std::vector<VisualDesc> parse_visuals(const toml::table& layer_tbl,
                                      std::vector<std::string>* diagnostics);

std::optional<LayerDesc> parse_layer(const toml::table& tbl, std::vector<std::string>* diagnostics);
std::optional<Camera2_5DAuthoring> parse_camera_2_5d(const toml::table& tbl,
                                                      std::vector<std::string>* diagnostics);
Camera parse_render_camera(const toml::table& tbl);

std::optional<SpecSceneDocument> parse_document(const toml::table& root,
                                                std::vector<std::string>* diagnostics);

} // namespace chronon3d::specscene
