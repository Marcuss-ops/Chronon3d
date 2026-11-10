#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
namespace chronon3d::render_plan::detail {


LayerPlan::StaggerPlan decode_stagger(const nlohmann::json& raw) {
    LayerPlan::StaggerPlan plan;
    plan.per_item_frames = raw.value("per_item_frames", plan.per_item_frames);
    plan.order = raw.value("order", plan.order);
    plan.seed = raw.value("seed", plan.seed);
    plan.track_property = raw.value("track_property", plan.track_property);
    plan.easing = raw.value("easing", plan.easing);
    if (plan.per_item_frames > 600 ||
        (plan.order != "forward" && plan.order != "reverse" &&
         plan.order != "center_out" && plan.order != "random" &&
         plan.order != "from_edges" && plan.order != "spatial_x" &&
         plan.order != "spatial_y" && plan.order != "radial") ||
        plan.track_property.empty() || plan.track_property.size() > 128)
        throw std::runtime_error("layout stagger has invalid delay, order, or track_property");
    if (!plan.easing.empty()) {
        if (!is_stagger_easing(plan.easing))
            throw std::runtime_error("layout stagger has an unsupported easing");
    }
    return plan;
}

void decode_layer_follow_path(LayerPlan& layer, const nlohmann::json& value) {
    if (!value.contains("follow_path")) return;

    const auto& raw = value.at("follow_path");
    LayerPlan::FollowPathDriverPlan driver;
    if (raw.contains("path_id")) driver.path_id = raw.at("path_id").get<std::string>();
    if (raw.contains("path_commands"))
        driver.path_commands = decode_follow_path_commands(raw.at("path_commands"));
    driver.progress = decode_animation_track(raw.at("progress"));
    driver.progress.property = "follow_path_progress";
    for (const auto& key : driver.progress.keyframes)
        if (key.value.size() != 1 || !std::isfinite(key.value.front()))
            throw std::runtime_error("follow_path progress values must be finite scalars");

    driver.auto_orient = raw.value("auto_orient", false);
    driver.offset_deg = raw.value("offset_deg", 0.0f);
    driver.clamp_end = raw.value("clamp_end", true);
    if (!std::isfinite(driver.offset_deg) || std::abs(driver.offset_deg) > 1000000.0f)
        throw std::runtime_error("follow_path offset_deg is outside supported bounds");
    layer.follow_path = std::move(driver);
}

std::vector<ShapePathCommandPlan> decode_follow_path_commands(const nlohmann::json& raw) {
    if (!raw.is_array() || raw.size() < 2 || raw.size() > 65536)
        throw std::runtime_error("follow_path path_commands must contain 2..65536 commands");
    std::vector<ShapePathCommandPlan> commands;
    commands.reserve(raw.size());
    bool has_move = false;
    bool closed = false;
    for (const auto& item : raw) {
        auto command = decode_shape_path_command(item, "follow_path command");
        if (closed) throw std::runtime_error("follow_path commands cannot continue after close");
        if (command.type == ShapePathCommandTypePlan::MoveTo) {
            if (has_move || !commands.empty())
                throw std::runtime_error("follow_path accepts one continuous subpath");
            has_move = true;
        } else if (!has_move) {
            throw std::runtime_error("follow_path must start with move_to");
        }
        for (const auto* point : {&command.point, &command.control1, &command.control2})
            if (std::abs((*point)[0]) > 1000000.0f || std::abs((*point)[1]) > 1000000.0f)
                throw std::runtime_error("follow_path coordinates must be within [-1000000,1000000]");
        closed = command.type == ShapePathCommandTypePlan::Close;
        commands.push_back(command);
    }
    if (!has_move || commands.size() < 2)
        throw std::runtime_error("follow_path requires move_to and at least one segment");
    return commands;
}





void decode_layer_text_counter(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("text_counter")) {
        if (layer.type != LayerType::Text)
            throw std::runtime_error("text_counter is only valid for text layers");
        const auto& raw = value.at("text_counter");
        if (!raw.is_object())
            throw std::runtime_error("text_counter must be an object");
        TextCounterPlan plan;
        plan.token = raw.value("token", plan.token);
        if (plan.token.empty() || plan.token.size() > 64 ||
            layer.text.find(plan.token) == std::string::npos ||
            layer.text.find(plan.token, layer.text.find(plan.token) + plan.token.size()) != std::string::npos)
            throw std::runtime_error("text_counter token must occur exactly once in text and contain at most 64 bytes");
        const auto& counter = raw.at("counter");
        if (!counter.is_object())
            throw std::runtime_error("text_counter.counter must be an object");
        auto& target = plan.counter;
        target.from = counter.at("from").get<float>();
        target.to = counter.at("to").get<float>();
        target.start_frame = Frame{counter.value("start_frame", std::int64_t{0})};
        target.duration_frames = Frame{counter.value("duration_frames", std::int64_t{30})};
        target.easing = counter.value("easing", std::string{"out_expo"});
        target.format = counter.value("format", std::string{"plain"});
        target.currency = counter.value("currency", std::string{"USD"});
        target.decimals = counter.value("decimals", 0);
        target.prefix = counter.value("prefix", std::string{});
        target.suffix = counter.value("suffix", std::string{});
        if (!std::isfinite(target.from) || !std::isfinite(target.to) ||
            std::abs(target.from) > 1.0e12f || std::abs(target.to) > 1.0e12f ||
            target.start_frame.integral() < 0 || target.start_frame.integral() > 1000000 ||
            target.duration_frames.integral() < 1 || target.duration_frames.integral() > 1000000 ||
            !is_counter_easing(target.easing) ||
            (target.format != "plain" && target.format != "currency" &&
             target.format != "percent" && target.format != "compact") ||
            target.decimals < 0 || target.decimals > 6 || target.prefix.size() > 32 ||
            target.suffix.size() > 32 || target.currency.size() > 8)
            throw std::runtime_error("text_counter values are outside supported bounds");
        if (static_cast<std::uint64_t>(target.start_frame.integral()) +
                static_cast<std::uint64_t>(target.duration_frames.integral()) > 1000000U)
            throw std::runtime_error("text_counter animation exceeds the supported frame limit");
        layer.text_counter = std::move(plan);
    }

}

void decode_layer_text_transitions(LayerPlan& layer, const nlohmann::json& value) {
    if (!value.contains("text_transitions")) return;
    if (layer.type != LayerType::Text)
        throw std::runtime_error("text_transitions is only valid for text layers");
    if (layer.text_counter)
        throw std::runtime_error("text_counter and text_transitions are mutually exclusive");

    const auto& raw = value.at("text_transitions");
    if (!raw.is_object())
        throw std::runtime_error("text_transitions must be an object");
    const auto reject_unknown = [](const nlohmann::json& object,
                                   std::initializer_list<std::string_view> allowed,
                                   std::string_view field) {
        if (!object.is_object())
            throw std::runtime_error(std::string(field) + " must be an object");
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
                throw std::runtime_error(std::string(field) + " has unsupported field '" +
                                         it.key() + "'");
        }
    };
    reject_unknown(raw, {"keyframes"}, "text_transitions");
    if (!raw.contains("keyframes") || !raw.at("keyframes").is_array() ||
        raw.at("keyframes").empty() || raw.at("keyframes").size() > 256)
        throw std::runtime_error("text_transitions.keyframes must contain 1..256 entries");

    TextTransitionsPlan plan;
    std::int64_t previous_frame = -1;
    for (const auto& entry : raw.at("keyframes")) {
        reject_unknown(entry, {"frame", "text", "transition", "scramble",
                               "morph", "typewriter"},
                       "text_transitions keyframe");
        if (!entry.contains("frame") || !entry.contains("text"))
            throw std::runtime_error("text_transitions keyframe requires frame and text");
        const auto frame = entry.at("frame").get<std::int64_t>();
        if (frame < 0 || frame > 1000000 || frame <= previous_frame)
            throw std::runtime_error("text_transitions frames must be strictly increasing in [0, 1000000]");
        previous_frame = frame;
        const auto text = entry.at("text").get<std::string>();
        if (text.size() > 65536)
            throw std::runtime_error("text_transitions text exceeds 65536 bytes");

        TextTransitionKeyframePlan keyframe;
        keyframe.frame = Frame{frame};
        keyframe.text = text;
        keyframe.transition = entry.value("transition", keyframe.transition);
        if (keyframe.transition != "hold" && keyframe.transition != "cut" &&
            keyframe.transition != "dissolve_layouts" &&
            keyframe.transition != "scramble" && keyframe.transition != "morph" &&
            keyframe.transition != "typewriter")
            throw std::runtime_error("text_transitions has an unsupported transition");

        if (entry.contains("scramble")) {
            if (keyframe.transition != "scramble")
                throw std::runtime_error("text_transitions.scramble requires transition='scramble'");
            const auto& params = entry.at("scramble");
            reject_unknown(params, {"seed", "char_pool", "intensity"},
                           "text_transitions.scramble");
            if (params.contains("seed")) {
                if (!params.at("seed").is_number_unsigned())
                    throw std::runtime_error("text_transitions.scramble.seed must be an unsigned integer");
                keyframe.scramble.seed = params.at("seed").get<std::uint64_t>();
            }
            if (params.contains("char_pool")) {
                keyframe.scramble.char_pool = params.at("char_pool").get<std::string>();
                if (keyframe.scramble.char_pool.empty() ||
                    keyframe.scramble.char_pool.size() > 64)
                    throw std::runtime_error("text_transitions.scramble.char_pool must contain 1..64 bytes");
            }
            if (params.contains("intensity")) {
                keyframe.scramble.intensity = params.at("intensity").get<float>();
                if (!std::isfinite(keyframe.scramble.intensity) ||
                    keyframe.scramble.intensity < 0.0f || keyframe.scramble.intensity > 10.0f)
                    throw std::runtime_error("text_transitions.scramble.intensity must be finite and in [0, 10]");
            }
        }

        if (entry.contains("morph")) {
            if (keyframe.transition != "morph")
                throw std::runtime_error("text_transitions.morph requires transition='morph'");
            const auto& params = entry.at("morph");
            reject_unknown(params, {"direction", "easing"}, "text_transitions.morph");
            if (params.contains("direction")) {
                keyframe.morph.direction = params.at("direction").get<std::string>();
                if (keyframe.morph.direction != "from_right" &&
                    keyframe.morph.direction != "from_left" &&
                    keyframe.morph.direction != "from_center")
                    throw std::runtime_error("text_transitions.morph.direction is unsupported");
            }
            if (params.contains("easing")) {
                keyframe.morph.easing = params.at("easing").get<std::string>();
                if (!is_standard_easing(keyframe.morph.easing))
                    throw std::runtime_error("text_transitions.morph.easing is unsupported");
            }
        }

        if (entry.contains("typewriter")) {
            if (keyframe.transition != "typewriter")
                throw std::runtime_error("text_transitions.typewriter requires transition='typewriter'");
            const auto& params = entry.at("typewriter");
            reject_unknown(params, {"chars_per_frame", "easing"},
                           "text_transitions.typewriter");
            if (params.contains("chars_per_frame")) {
                keyframe.typewriter.chars_per_frame = params.at("chars_per_frame").get<float>();
                if (!std::isfinite(keyframe.typewriter.chars_per_frame) ||
                    keyframe.typewriter.chars_per_frame < 0.0f ||
                    keyframe.typewriter.chars_per_frame > 1000.0f)
                    throw std::runtime_error("text_transitions.typewriter.chars_per_frame must be finite and in [0, 1000]");
            }
            if (params.contains("easing")) {
                keyframe.typewriter.easing = params.at("easing").get<std::string>();
                if (!is_standard_easing(keyframe.typewriter.easing))
                    throw std::runtime_error("text_transitions.typewriter.easing is unsupported");
            }
        }

        plan.keyframes.push_back(std::move(keyframe));
    }
    layer.text_transitions = std::move(plan);
}

void decode_layer_transform_fields(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("position")) {
        const auto& position = value.at("position");
        layer.position_dimensions = position.size();
        for (std::size_t i = 0; i < position.size() && i < layer.position.size(); ++i) {
            layer.position[i] = position.at(i).get<float>();
            if (!std::isfinite(layer.position[i]))
                throw std::runtime_error("layer.position components must be finite");
        }
    }

    if (value.contains("scale")) {
        const auto& scale = value.at("scale");
        layer.scale_dimensions = scale.size();
        for (std::size_t i = 0; i < scale.size() && i < layer.scale.size(); ++i)
            layer.scale[i] = scale.at(i).get<float>();
    }
    if (value.contains("rotation")) {
        const auto& rotation = value.at("rotation");
        layer.rotation_dimensions = rotation.size();
        for (std::size_t i = 0; i < rotation.size() && i < layer.rotation.size(); ++i)
            layer.rotation[i] = rotation.at(i).get<float>();
    }
    layer.skew_angle_deg = value.value("skew", 0.0f);
    layer.skew_axis_deg = value.value("skew_axis", 0.0f);
    if (!std::isfinite(layer.skew_angle_deg) || std::abs(layer.skew_angle_deg) > 80.0f ||
        !std::isfinite(layer.skew_axis_deg) || std::abs(layer.skew_axis_deg) > 1000000.0f)
        throw std::runtime_error("layer skew and skew_axis are outside supported bounds");

}

void decode_layer_layout(LayerPlan& layer, const nlohmann::json& value) {
    const auto parse_layout_vec2 = [](const nlohmann::json& raw, const char* key) {
        const auto& v = raw.at(key);
        if (!v.is_array() || v.size() != 2) throw std::runtime_error(std::string("layout.") + key + " must be [x,y]");
        std::array<float,2> result{v[0].get<float>(),v[1].get<float>()};
        if (!std::isfinite(result[0]) || !std::isfinite(result[1]) || result[0] < 0 || result[1] < 0)
            throw std::runtime_error(std::string("layout.") + key + " values must be finite and non-negative");
        return result;
    };
    if (value.contains("flow")) {
        const auto& raw=value.at("flow"); LayerPlan::FlowLayoutPlan p;
        p.group_id=raw.at("group_id").get<std::string>(); p.axis=raw.value("axis",p.axis);
        p.align=raw.value("align",p.align); p.justify=raw.value("justify",p.justify);
        p.gap=raw.value("gap",0.0f); p.wrap=raw.value("wrap",false);
        if (raw.contains("cell_size")) p.cell_size=parse_layout_vec2(raw,"cell_size");
        p.intrinsic_size=raw.value("intrinsic_size",false);
        if (raw.contains("stagger")) p.stagger=decode_stagger(raw.at("stagger"));
        static constexpr std::array<std::string_view,2> axes{"row","column"};
        static constexpr std::array<std::string_view,4> aligns{"start","center","end","stretch"};
        static constexpr std::array<std::string_view,6> justifies{"start","center","end","space_between","space_around","space_evenly"};
        if (p.group_id.empty() || p.group_id.size()>256 || !std::isfinite(p.gap) || p.gap<0 || p.gap>1000000 ||
            std::find(axes.begin(),axes.end(),p.axis)==axes.end() || std::find(aligns.begin(),aligns.end(),p.align)==aligns.end() || std::find(justifies.begin(),justifies.end(),p.justify)==justifies.end())
            throw std::runtime_error("flow layout has invalid group, axis, alignment, justification, or gap");
        layer.flow_layout=std::move(p);
    }
    if (value.contains("grid_layout")) {
        const auto& raw=value.at("grid_layout"); LayerPlan::GridLayoutPlan p;
        p.group_id=raw.at("group_id").get<std::string>(); p.fit=raw.value("fit",p.fit);
        p.columns=raw.value("columns",1); p.rows=raw.value("rows",1);
        if (raw.contains("gap")) p.gap=parse_layout_vec2(raw,"gap");
        if (raw.contains("cell_size")) p.cell_size=parse_layout_vec2(raw,"cell_size");
        p.intrinsic_size=raw.value("intrinsic_size",false);
        if (raw.contains("stagger")) p.stagger=decode_stagger(raw.at("stagger"));
        if (p.group_id.empty() || p.group_id.size()>256 || p.columns<1 || p.columns>4096 || p.rows<1 || p.rows>4096 ||
            (p.fit!="auto" && p.fit!="fixed_columns" && p.fit!="fixed_rows"))
            throw std::runtime_error("grid layout has invalid group, dimensions, or fit");
        layer.grid_layout=std::move(p);
    }
    if (value.contains("stagger_group")) {
        const auto& raw = value.at("stagger_group");
        auto stagger = decode_stagger(raw);
        stagger.group_id = raw.at("group_id").get<std::string>();
        if (stagger.group_id.empty() || stagger.group_id.size() > 256)
            throw std::runtime_error("stagger_group group_id must contain 1..256 characters");
        layer.stagger_group = std::move(stagger);
    }
    if (layer.flow_layout && layer.grid_layout) throw std::runtime_error("a layer cannot belong to flow and grid layouts simultaneously");
 }



} // namespace chronon3d::render_plan::detail
