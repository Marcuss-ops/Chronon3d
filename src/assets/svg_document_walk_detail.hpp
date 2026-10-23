#pragma once

#include "svg_gradient_detail.hpp"
#include "svg_text_detail.hpp"
#include <chronon3d/assets/svg_path_loader.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::assets::svg_document_walk_detail {

using SvgIdMap = svg_text_detail::SvgIdMap;

struct SvgStyleState {
    std::optional<Fill> fill{Fill::solid_color(Color::black())};
    PathStroke stroke{.enabled = false};
    // SVG's initial stroke-miterlimit is 4 (native Chronon paths retain 10).
    float stroke_miter_limit{4.0f};
    Color current_color{Color::black()};
    PathFillRule fill_rule{PathFillRule::NonZero};
    float opacity{1.0f};
    float fill_opacity{1.0f};
    float stroke_opacity{1.0f};
    bool display_none{false};
    bool visibility_hidden{false};
    std::string fill_gradient_id;
    std::string paint_order{"normal"};
    std::string filter_id;
    std::string vector_mask_id;
    bool vector_mask_is_mask{false};
    struct VectorMaskScope {
        std::string id;
        bool is_mask{false};
        svg_gradient_detail::Affine2 transform{1.0f};
    };
    std::vector<VectorMaskScope> vector_mask_scopes;
    std::vector<PathShape> vector_masks;
    std::string source_id;
    std::vector<std::string> group_ids;
    std::vector<SvgAnimationSpec> inherited_animations;
    std::optional<SvgMorphAnimationSpec> morph_animation;
    // ADR-035 — isolated SVG groups enclosing this element, outermost first.
    std::vector<SvgStyleGroupRef> groups;
};

void collect_path_data(
    const boost::property_tree::ptree& tree,
    std::vector<std::pair<std::string, svg_gradient_detail::Affine2>>& paths,
    std::vector<SvgStyleState>& styles,
    std::vector<std::vector<SvgAnimationSpec>>& animations,
    const svg_gradient_detail::SvgGradientMap& gradients,
    const SvgIdMap& ids,
    std::size_t use_depth,
    std::size_t& expanded_uses,
    std::uint32_t& isolated_group_counter,
    const svg_gradient_detail::Affine2& parent_transform =
        svg_gradient_detail::Affine2(1.0f),
    SvgStyleState parent_style = {});

std::optional<std::string> collect_vector_mask_geometry(
    const std::string& definition_kind,
    const boost::property_tree::ptree& definition,
    const svg_gradient_detail::Affine2& root_transform,
    PathShape& output);

}  // namespace chronon3d::assets::svg_document_walk_detail
