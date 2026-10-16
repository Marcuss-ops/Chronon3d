#include <doctest/doctest.h>

#include <chrononmotion/motion/OfflineBaking.hpp>
#include <chrononmotion/motion/CameraRig.hpp>
#include <chrononmotion/motion/RenderPlanLowering.hpp>
#include <chrononmotion/motion/RenderPlanEmission.hpp>
#include <chrononmotion/motion/MotionScene.hpp>

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include "src/render_plan/render_plan_compiler_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <string>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::RenderPlan;
using Json = nlohmann::json;

namespace {

Json jsonPathCommands(const std::vector<chronon3d::render_plan::ShapePathCommandPlan>& commands) {
    Json path = Json::array();
    for (const auto& command : commands) {
        switch (command.type) {
            case chronon3d::render_plan::ShapePathCommandTypePlan::MoveTo:
                path.push_back({{"type", "move_to"}, {"point", command.point}}); break;
            case chronon3d::render_plan::ShapePathCommandTypePlan::LineTo:
                path.push_back({{"type", "line_to"}, {"point", command.point}}); break;
            case chronon3d::render_plan::ShapePathCommandTypePlan::QuadraticTo:
                path.push_back({{"type", "quadratic_to"}, {"point", command.point},
                                {"control1", command.control1}}); break;
            case chronon3d::render_plan::ShapePathCommandTypePlan::CubicTo:
                path.push_back({{"type", "cubic_to"}, {"point", command.point},
                                {"control1", command.control1}, {"control2", command.control2}}); break;
            case chronon3d::render_plan::ShapePathCommandTypePlan::Close:
                path.push_back({{"type", "close"}}); break;
        }
    }
    return path;
}

Json jsonForValidation(const RenderPlan& plan) {
    Json root{{"schema", plan.schema}, {"version", plan.schema == chronon3d::render_plan::kRenderPlanSchemaV3 ? 3 : 2},
              {"job_id", plan.job_id},
              {"canvas", {{"width", plan.canvas.width}, {"height", plan.canvas.height},
                           {"fps_num", plan.canvas.fps.numerator}, {"fps_den", plan.canvas.fps.denominator},
                           {"duration_frames", plan.canvas.duration.integral()}}},
              {"output", {{"path", plan.output.path}, {"format", "png"}}},
              {"layers", Json::array()}};
    if (plan.camera) {
        root["camera"] = {{"type", plan.camera->type}, {"position", plan.camera->position},
                          {"rotation_deg", plan.camera->rotation_deg}, {"fov_deg", plan.camera->fov_deg},
                          {"near", plan.camera->near}, {"far", plan.camera->far}, {"zoom", plan.camera->zoom}};
    }
    if (plan.camera_animation) {
        Json animation{{"tracks", Json::array()}};
        for (const auto& track : *plan.camera_animation) {
            const char* property = "camera_position_z";
            using Property = chronon3d::render_plan::CameraPropertyPlan;
            switch (track.property) {
                case Property::PositionX: property = "camera_position_x"; break;
                case Property::PositionY: property = "camera_position_y"; break;
                case Property::PositionZ: property = "camera_position_z"; break;
                case Property::RotationX: property = "camera_rotation_x"; break;
                case Property::RotationY: property = "camera_rotation_y"; break;
                case Property::RotationZ: property = "camera_rotation_z"; break;
                case Property::FovDeg: property = "camera_fov_deg"; break;
                case Property::Zoom: property = "camera_zoom"; break;
            }
            Json encoded{{"property", property}, {"easing", track.easing}, {"keyframes", Json::array()}};
            for (const auto& key : track.keyframes)
                encoded["keyframes"].push_back({{"frame", key.frame.integral()},
                                                {"value", key.value.empty() ? 0.f : key.value.front()}});
            animation["tracks"].push_back(std::move(encoded));
        }
        root["camera_animation"] = std::move(animation);
    }
    for (const auto& layer : plan.layers) {
        const char* type = "color";
        switch (layer.type) {
            case LayerType::Image: type = "image"; break;
            case LayerType::Video: type = "video"; break;
            case LayerType::Text: type = "text"; break;
            case LayerType::Color: type = "color"; break;
            case LayerType::Shape: type = "shape"; break;
            case LayerType::Particles: type = "particles"; break;
            case LayerType::Light: type = "light"; break;
            case LayerType::Adjustment: type = "adjustment"; break;
        }
        Json value{{"id", layer.id}, {"type", type}, {"color", layer.color},
                   {"position", layer.position}, {"scale", layer.scale}, {"rotation", layer.rotation},
                   {"enable_3d", layer.enable_3d}};
        if (!layer.asset.empty()) value["asset"] = layer.asset;
        if (!layer.source.empty()) value["source"] = layer.source;
        if (layer.size_dimensions == 2) value["size"] = {layer.size[0], layer.size[1]};
        if (layer.opacity) value["opacity"] = *layer.opacity;
        if (layer.start_frame) value["start_frame"] = layer.start_frame->integral();
        if (layer.duration_frames) value["duration_frames"] = layer.duration_frames->integral();
        if (layer.safe_insets)
            value["safe_insets"] = {{"top", (*layer.safe_insets)[0]},
                                     {"right", (*layer.safe_insets)[1]},
                                     {"bottom", (*layer.safe_insets)[2]},
                                     {"left", (*layer.safe_insets)[3]}};
        if (layer.type == LayerType::Text) {
            value["text"] = layer.text;
            if (layer.style) {
                value["style"] = {{"font", layer.style->font}, {"fill", layer.style->fill}};
                if (layer.style->font_size) value["style"]["font_size"] = *layer.style->font_size;
                else if (layer.type == LayerType::Text) value["style"]["font_size"] = 24.f;
            }
            if (!layer.text_spans.empty()) {
                value["spans"] = Json::array();
                for (const auto& span : layer.text_spans) {
                    Json encoded{{"start", span.byte_start}, {"end", span.byte_end}};
                    if (span.semantic_id) encoded["semantic_id"] = *span.semantic_id;
                    const bool has_style = span.color || span.font_size_multiplier || span.tracking ||
                                           span.stroke || span.highlight;
                    if (has_style) {
                        encoded["style"] = Json::object();
                        if (span.color) {
                            const auto channel = [](float linear) {
                                return static_cast<int>(std::lround(
                                    chronon3d::render_plan::linear_to_srgb(linear) * 255.f));
                            };
                            char hex[10];
                            std::snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X",
                                channel(span.color->r), channel(span.color->g),
                                channel(span.color->b), static_cast<int>(std::lround(span.color->a * 255.f)));
                            encoded["style"]["color"] = hex;
                        }
                        if (span.font_size_multiplier) encoded["style"]["font_size_mult"] = *span.font_size_multiplier;
                        if (span.tracking) encoded["style"]["tracking"] = *span.tracking;
                    }
                    value["spans"].push_back(std::move(encoded));
                }
            }
            if (!layer.text_animators.empty()) {
                value["text_animators"] = Json::array();
                for (const auto& animator : layer.text_animators) {
                    Json encoded{{"selectors", Json::array()}, {"properties", Json::array()}};
                    if (!animator.id.empty()) encoded["id"] = animator.id;
                    for (const auto& selector : animator.selectors) {
                        Json selector_json{{"unit", selector.unit}, {"shape", selector.shape},
                                           {"order", selector.order}, {"combine", selector.combine},
                                           {"exclude_spaces", selector.exclude_spaces},
                                           {"randomize_order", selector.randomize_order}};
                        if (!selector.id.empty()) selector_json["id"] = selector.id;
                        if (selector.semantic_id) selector_json["semantic_id"] = *selector.semantic_id;
                        if (selector.order == "index") selector_json["index_step"] = selector.index_step;
                        for (const auto& [key, track] : std::array<std::pair<const char*, const chronon3d::render_plan::AnimationTrackPlan*>, 4>{{
                                 {"start", &selector.start}, {"end", &selector.end},
                                 {"offset", &selector.offset}, {"amount", &selector.amount}}}) {
                            if (track->keyframes.empty()) continue;
                            Json track_json{{"easing", track->easing}, {"keyframes", Json::array()}};
                            for (const auto& keyframe : track->keyframes)
                                track_json["keyframes"].push_back({{"frame", keyframe.frame.integral()},
                                                                  {"value", keyframe.value.size() == 1
                                                                      ? Json(keyframe.value.front())
                                                                      : Json(keyframe.value)}});
                            selector_json[key] = std::move(track_json);
                        }
                        encoded["selectors"].push_back(std::move(selector_json));
                    }
                    for (const auto& track : animator.properties) {
                        Json track_json{{"property", track.property}, {"easing", track.easing},
                                        {"keyframes", Json::array()}};
                        for (const auto& keyframe : track.keyframes)
                            track_json["keyframes"].push_back({{"frame", keyframe.frame.integral()},
                                                              {"value", keyframe.value.size() == 1
                                                                  ? Json(keyframe.value.front())
                                                                  : Json(keyframe.value)}});
                        encoded["properties"].push_back(std::move(track_json));
                    }
                    value["text_animators"].push_back(std::move(encoded));
                }
            }
        }
        if (layer.animation) {
            Json animation{{"tracks", Json::array()}};
            for (const auto& track : layer.animation->tracks) {
                Json encoded{{"property", track.property}, {"easing", track.easing}, {"keyframes", Json::array()}};
                for (const auto& key : track.keyframes)
                    encoded["keyframes"].push_back({{"frame", key.frame.integral()}, {"value", key.value}});
                animation["tracks"].push_back(std::move(encoded));
            }
            value["animation"] = std::move(animation);
        }
        if (layer.shape) {
            const char* shapeType = "path";
            switch (layer.shape->type) {
                case chronon3d::render_plan::ShapeContentTypePlan::Rect: shapeType = "rect"; break;
                case chronon3d::render_plan::ShapeContentTypePlan::RoundedRect: shapeType = "rounded_rect"; break;
                case chronon3d::render_plan::ShapeContentTypePlan::Ellipse: shapeType = "ellipse"; break;
                case chronon3d::render_plan::ShapeContentTypePlan::Line: shapeType = "line"; break;
                case chronon3d::render_plan::ShapeContentTypePlan::Path: shapeType = "path"; break;
            }
            Json shape{{"type", shapeType}};
            if (layer.shape->type == chronon3d::render_plan::ShapeContentTypePlan::Path ||
                layer.shape->type == chronon3d::render_plan::ShapeContentTypePlan::Line)
                shape["path"] = jsonPathCommands(layer.shape->path);
            if (layer.shape->fill_color) shape["fill"] = *layer.shape->fill_color;
            if (!layer.shape->operators.empty()) {
                Json operators = Json::array();
                for (const auto& op : layer.shape->operators) {
                    using Kind = chronon3d::render_plan::PathOperatorKindPlan;
                    const char* kind = "repeater";
                    Json params = Json::object();
                    switch (op.kind) {
                        case Kind::Merge: {
                            kind = "merge";
                            params["paths"] = Json::array();
                            for (const auto& mergedPath : op.paths) params["paths"].push_back(jsonPathCommands(mergedPath));
                            break;
                        }
                        case Kind::Morph:
                            kind = "morph";
                            params["target"] = jsonPathCommands(op.morph_target);
                            params["progress"] = op.morph_progress;
                            if (op.morph_animation) {
                                Json morphAnimation{{"easing", op.morph_animation->easing}, {"keyframes", Json::array()}};
                                for (const auto& key : op.morph_animation->keyframes)
                                    morphAnimation["keyframes"].push_back({{"frame", key.frame.integral()},
                                        {"value", key.value.empty() ? 0.f : key.value.front()}});
                                params["animation"] = std::move(morphAnimation);
                            }
                            break;
                        case Kind::Repeater:
                            kind = "repeater";
                            params = {{"copies", op.repeater.copies}, {"position", op.repeater.position},
                                      {"scale", op.repeater.scale}, {"rotation_degrees", op.repeater.rotation_degrees},
                                      {"anchor", op.repeater.anchor}, {"opacity_decay", op.repeater.opacity_decay}};
                            break;
                        case Kind::RoundCorners: kind = "round_corners"; params["radius"] = op.radius; break;
                        case Kind::ZigZag: kind = "zig_zag"; params = {{"size", op.size}, {"points_per_unit", op.points_per_unit}, {"smooth", op.smooth}}; break;
                        case Kind::OffsetPath: kind = "offset_path"; params = {{"amount", op.amount}, {"miter_limit", op.miter_limit}}; break;
                        case Kind::PuckerBloat: kind = "pucker_bloat"; params = {{"amount", op.amount}, {"anchor", op.anchor}}; break;
                        case Kind::Twist: kind = "twist"; params = {{"degrees", op.degrees}, {"center", op.center}}; break;
                        case Kind::WigglePath: kind = "wiggle_path"; params = {{"amount", op.amount}, {"frequency", op.frequency}, {"seed", op.seed}}; break;
                        case Kind::Trim: kind = "trim"; params = {{"start", op.trim_start}, {"end", op.trim_end}, {"offset", op.trim_offset}}; break;
                    }
                    operators.push_back({{"kind", kind}, {"params", std::move(params)}});
                }
                shape["operators"] = std::move(operators);
            }
            value["shape"] = std::move(shape);
        }
        root["layers"].push_back(std::move(value));
    }
    return root;
}

bool canonicallyValid(const RenderPlan& plan, std::string* diagnostic = nullptr) {
    const Json json = Json::parse(renderPlanToJson(plan));
    const auto validation = chronon3d::render_plan::validate_render_plan(json);
    if (!validation.ok()) {
        if (diagnostic) *diagnostic = validation.format();
        return false;
    }
    auto decoded = chronon3d::render_plan::decode_render_plan(json);
    if (!decoded) {
        if (diagnostic) {
            const auto& error = decoded.error();
            *diagnostic = error.path + ": " + error.message +
                (error.code.empty() ? std::string{} : " [" + error.code + "]");
        }
        return false;
    }
    return true;
}

std::string canonicalDiagnostic(const RenderPlan& plan) {
    std::string diagnostic;
    (void)canonicallyValid(plan, &diagnostic);
    return diagnostic;
}

void checkSchemaValid(const Json& json) {
    const auto validation = chronon3d::render_plan::validate_render_plan(json);
    CHECK_MESSAGE(validation.ok(), validation.format());
}

void checkSemanticallyRejected(const Json& json) {
    const auto decoded = chronon3d::render_plan::decode_render_plan(json);
    if (!decoded) return;
    CHECK_MESSAGE(!decoded.has_value(), "invalid Morph topology unexpectedly decoded");
}

} // namespace

TEST_CASE("ChrononMotion offline audio baking lowers into ordinary V3 transform tracks") {
    MotionScene scene("audio-reactive", 10.f);
    auto& driven = scene.addContent(
        41, "audio-driven", ContentRef::image("audio-driven-image", {32.f, 32.f}));
    driven.lifetime = FrameRange::between(0, 2);
    const std::array<AudioFeatureSample, 3> features{{
        {0.f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.1f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.2f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f}}};
    bakeAudioDriverToLayer(driven, features,
        AudioDriverSpec{AudioFeature::Rms, 12.f, 0.f, 0.f},
        AudioDriverProperty::Position, Vector3(1.f, 0.f, 0.f), 10.f, 0, 2);

    RenderPlanLoweringOptions options;
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    options.width = 64;
    options.height = 64;
    LayerPlan image;
    image.type = LayerType::Image;
    image.asset = "audio-driven-image";
    image.size = {32.f, 32.f};
    image.size_dimensions = 2;
    options.contentLayers.emplace("41", image);
    const auto plan = lowerToRenderPlan(scene, options);

    REQUIRE(plan.schema == chronon3d::render_plan::kRenderPlanSchemaV3);
    REQUIRE(plan.layers.size() == 1);
    REQUIRE(plan.layers[0].animation.has_value());
    const auto position = std::find_if(plan.layers[0].animation->tracks.begin(),
        plan.layers[0].animation->tracks.end(), [](const auto& track) {
            return track.property == "position";
        });
    REQUIRE(position != plan.layers[0].animation->tracks.end());
    REQUIRE(position->keyframes.size() >= 3);
    CHECK(position->keyframes[0].value[0] < position->keyframes[1].value[0]);
    CHECK(position->keyframes[1].value[0] < position->keyframes[2].value[0]);
    CHECK_MESSAGE(canonicallyValid(plan), canonicalDiagnostic(plan));
}

TEST_CASE("ChrononMotion video time remap emits and decodes canonical V3 plans") {
    using chronon3d::render_plan::kRenderPlanSchemaV2;
    using chronon3d::render_plan::kRenderPlanSchemaV3;

    MotionScene scene("video-time-remap", 30.f);
    auto& video = scene.addContent(51, "video", ContentRef::video("source.mp4", {640.f, 360.f}));
    video.lifetime = FrameRange::between(0, 3);
    video.timeRemap.stretch = 2.f;
    RenderPlanLoweringOptions options;
    options.schema = kRenderPlanSchemaV3;
    options.width = 640;
    options.height = 360;
    LayerPlan supplied;
    supplied.type = LayerType::Video;
    supplied.source = "source.mp4";
    options.contentLayers.emplace("51", supplied);

    const RenderPlan slowed = lowerToRenderPlan(scene, options);
    REQUIRE(slowed.layers.size() == 1);
    REQUIRE(slowed.layers[0].time_remap.has_value());
    CHECK(slowed.layers[0].time_remap->speed == doctest::Approx(0.5f));
    const std::string speedJson = renderPlanToJson(slowed);
    const Json speedDocument = Json::parse(speedJson);
    REQUIRE(speedDocument["layers"][0].contains("time_remap"));
    CHECK(speedDocument["layers"][0]["time_remap"]["speed"] == doctest::Approx(0.5f));
    CHECK_MESSAGE(canonicallyValid(slowed), canonicalDiagnostic(slowed));

    MotionScene animatedScene("animated-video-time-remap", 30.f);
    auto& animatedVideo = animatedScene.addContent(
        52, "animated video", ContentRef::video("animated.mp4", {640.f, 360.f}));
    animatedVideo.lifetime = FrameRange::between(0, 3);
    animatedVideo.timeRemapTrack.add(0.f, 0.f).add(1.f, 20.f);
    options.contentLayers.clear();
    supplied.source = "animated.mp4";
    options.contentLayers.emplace("52", supplied);
    const RenderPlan animated = lowerToRenderPlan(animatedScene, options);
    REQUIRE(animated.layers[0].time_remap.has_value());
    REQUIRE(animated.layers[0].time_remap->animation.has_value());
    CHECK(animated.layers[0].time_remap->animation->keyframes.size() == 31);
    CHECK(animated.layers[0].time_remap->animation->keyframes[15].value[0] == doctest::Approx(10.f));
    const auto decoded = chronon3d::render_plan::decode_render_plan(
        Json::parse(renderPlanToJson(animated)));
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].time_remap.has_value());
    REQUIRE(decoded->layers[0].time_remap->animation.has_value());
    CHECK(decoded->layers[0].time_remap->animation->keyframes.size() == 31);
    CHECK_MESSAGE(canonicallyValid(animated), canonicalDiagnostic(animated));

    options.schema = kRenderPlanSchemaV2;
    options.contentLayers.clear();
    supplied.source = "source.mp4";
    options.contentLayers.emplace("51", supplied);
    CHECK_THROWS_AS(lowerToRenderPlan(scene, options), std::invalid_argument);
}

TEST_CASE("ChrononMotion safe-area emission round-trips through canonical V3 contract") {
    MotionScene scene("safe-area-roundtrip", 30.f);
    scene.addContent(1, "title", ContentRef::text("title", {200.f, 60.f}));
    scene.evaluate(0);

    RenderPlanLoweringOptions options;
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    options.width = 640;
    options.height = 360;
    LayerPlan supplied;
    supplied.type = LayerType::Text;
    supplied.text = "Title";
    supplied.style = chronon3d::render_plan::LayerStylePlan{};
    supplied.style->font = "font.ttf";
    supplied.style->font_size = 24.f;
    supplied.style->fill = "#FFFFFF";
    supplied.safe_insets = std::array<float, 4>{20.f, 30.f, 40.f, 50.f};
    options.contentLayers.emplace("1", supplied);

    const RenderPlan plan = lowerToRenderPlan(scene, options);
    const std::string emitted = renderPlanToJson(plan);
    const Json document = Json::parse(emitted);
    REQUIRE(document["layers"][0].contains("safe_insets"));
    CHECK(document["layers"][0]["safe_insets"] ==
          Json{{"top", 20.f}, {"right", 30.f}, {"bottom", 40.f}, {"left", 50.f}});
    const auto decoded = chronon3d::render_plan::decode_render_plan(document);
    const std::string decode_error = decoded.has_value() ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    REQUIRE(decoded->layers[0].safe_insets.has_value());
    CHECK(*decoded->layers[0].safe_insets == *supplied.safe_insets);
    CHECK(renderPlanToJson(plan) == emitted);
    const auto validation = chronon3d::render_plan::validate_render_plan(document);
    CHECK_MESSAGE(validation.ok(), validation.format());
    const auto decodedPlan = chronon3d::render_plan::decode_render_plan(document);
    REQUIRE(decodedPlan);
    CHECK(decodedPlan->layers[0].safe_insets == supplied.safe_insets);
}

TEST_CASE("ChrononMotion semantic text emission round-trips through canonical V3 contract") {
    RenderPlan plan;
    plan.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    plan.job_id = "semantic-text-roundtrip";
    plan.canvas.width = 640;
    plan.canvas.height = 360;
    plan.canvas.fps = chronon3d::FrameRate{30, 1};
    plan.canvas.duration = chronon3d::Frame{30};
    plan.output.path = "out.png";
    LayerPlan layer;
    layer.id = "caption";
    layer.type = LayerType::Text;
    layer.text = "Hi future";
    layer.style = chronon3d::render_plan::LayerStylePlan{};
    layer.style->font = "fonts/roboto.ttf";
    layer.style->font_size = 32.f;
    layer.style->fill = "#FFFFFF";

    chronon3d::TextSpanOverride span;
    span.byte_start = 3;
    span.byte_end = 9;
    span.semantic_id = "word-0";
    span.color = *chronon3d::render_plan::parse_hex_color("#FF8000");
    span.font_size_multiplier = 1.25f;
    span.tracking = 0.02f;
    span.stroke = chronon3d::TextStrokeStyle{};
    span.stroke->width_em = 0.08f;
    span.highlight = chronon3d::TextHighlightStyle{};
    span.highlight->background = chronon3d::Color{0.f, 0.f, 0.f, 1.f};
    span.highlight->padding_em = 0.5f;
    span.highlight->radius_em = 0.2f;
    layer.text_spans.push_back(span);

    chronon3d::render_plan::TextSelectorPlan selector;
    selector.id = "word-selector";
    selector.unit = "word";
    selector.shape = "square";
    selector.semantic_id = "word-0";
    selector.amount.keyframes = {{chronon3d::Frame{0}, {0.f}},
                                 {chronon3d::Frame{8}, {100.f}}};
    chronon3d::render_plan::TextAnimatorPlan animator;
    animator.id = "word-highlight";
    animator.selectors.push_back(selector);
    chronon3d::render_plan::AnimationTrackPlan opacity;
    opacity.property = "opacity";
    opacity.keyframes = {{chronon3d::Frame{0}, {0.f}},
                         {chronon3d::Frame{8}, {1.f}}};
    chronon3d::render_plan::AnimationTrackPlan fill;
    fill.property = "fill_color";
    fill.keyframes = {{chronon3d::Frame{0}, {1.f, 0.7f, 0.f, 1.f}}};
    animator.properties = {opacity, fill};
    layer.text_animators.push_back(animator);
    plan.layers.push_back(layer);

    const std::string emitted = renderPlanToJson(plan);
    const Json actual = Json::parse(emitted);
    CHECK(actual.at("layers")[0].at("spans")[0].at("semantic_id") == "word-0");
    CHECK(actual.at("layers")[0].at("text_animators")[0].at("selectors")[0].at("semantic_id") == "word-0");
    CHECK(actual.at("layers")[0].at("text_animators")[0].at("properties")[1].at("property") == "fill_color");
    checkSchemaValid(actual);
    const auto decoded = chronon3d::render_plan::decode_render_plan(actual);
    REQUIRE(decoded);
    REQUIRE(decoded->layers.size() == 1);
    REQUIRE(decoded->layers[0].text_spans.size() == 1);
    CHECK(decoded->layers[0].text_spans[0].semantic_id == span.semantic_id);
    CHECK(decoded->layers[0].text_spans[0].font_size_multiplier == doctest::Approx(1.25f));
    CHECK(decoded->layers[0].text_spans[0].tracking == doctest::Approx(0.02f));
    REQUIRE(decoded->layers[0].text_animators.size() == 1);
    CHECK(decoded->layers[0].text_animators[0].selectors[0].semantic_id == selector.semantic_id);
    CHECK(decoded->layers[0].text_animators[0].properties[1].property == "fill_color");
    auto decodedForEmission = *decoded;
    decodedForEmission.content_fingerprint = 0; // Fingerprints are derived by decode and are not serialized.
    decodedForEmission.layers[0].font.clear(); // Normalized font paths are derived from style.font.
    CHECK(renderPlanToJson(decodedForEmission) == emitted);

    auto invalid = plan;
    invalid.layers[0].text_animators[0].selectors[0].semantic_id = "missing";
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
    invalid = plan;
    invalid.layers[0].text_spans[0].byte_end = invalid.layers[0].text_spans[0].byte_start;
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
    invalid = plan;
    invalid.layers[0].text_animators[0].properties[1].keyframes[0].value[0] = 1.5f;
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
    invalid = plan;
    invalid.layers[0].text_spans[0].semantic_id = "word-0";
    invalid.layers[0].text_spans.push_back(invalid.layers[0].text_spans[0]);
    invalid.layers[0].text_spans.back().byte_start = 0;
    invalid.layers[0].text_spans.back().byte_end = 2;
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
}

TEST_CASE("ChrononMotion blend-mode emission round-trips through canonical V2 and V3 contracts") {
    using chronon3d::BlendMode;
    using chronon3d::render_plan::kRenderPlanSchemaV2;
    using chronon3d::render_plan::kRenderPlanSchemaV3;

    MotionScene scene("blend-roundtrip", 30.f);
    scene.addContent(1, "relief", ContentRef::image("relief.png", {64.f, 64.f}));
    scene.evaluate(0);

    LayerPlan supplied;
    supplied.type = LayerType::Image;
    supplied.asset = "relief.png";
    supplied.size = {64.f, 64.f};
    supplied.size_dimensions = 2;
    supplied.blend_mode = BlendMode::SoftLight;

    for (const char* schema : {kRenderPlanSchemaV2, kRenderPlanSchemaV3}) {
        RenderPlanLoweringOptions options;
        options.schema = schema;
        options.width = 64;
        options.height = 64;
        options.contentLayers.emplace("1", supplied);

        const RenderPlan plan = lowerToRenderPlan(scene, options);
        REQUIRE(plan.layers.size() == 1);
        CHECK(plan.layers[0].blend_mode == BlendMode::SoftLight);

        const std::string emitted = renderPlanToJson(plan);
        const Json document = Json::parse(emitted);
        REQUIRE(document["layers"][0].contains("blend_mode"));
        CHECK(document["layers"][0]["blend_mode"] == "soft_light");
        const auto validation = chronon3d::render_plan::validate_render_plan(document);
        CHECK_MESSAGE(validation.ok(), validation.format());
        const auto decoded = chronon3d::render_plan::decode_render_plan(document);
        REQUIRE(decoded);
        REQUIRE(decoded->layers.size() == 1);
        CHECK(decoded->layers[0].blend_mode == BlendMode::SoftLight);
        CHECK(renderPlanToJson(plan) == emitted);
    }
}

TEST_CASE("ChrononMotion lowering emits schema-valid v2 and v3 render plans") {
    MotionScene scene("adapter", 30.f);
    scene.addNull(10, "Controller");
    auto& title = scene.addContent(11, "Visual", ContentRef::image("opaque-content", {100.f, 40.f}), 10);
    title.tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f));
    title.tracks.position.add(1.f, Vector3(30.f, 20.f, 0.f));
    scene.evaluate(0);

    RenderPlanLoweringOptions options;
    options.width = 640;
    options.height = 360;
    LayerPlan visual;
    visual.type = LayerType::Image;
    visual.asset = "opaque-content";
    visual.size = {100.f, 40.f};
    visual.size_dimensions = 2;
    options.contentLayers.emplace("11", visual);
    const auto plan = lowerToRenderPlan(scene, options);

    REQUIRE(plan.schema == chronon3d::render_plan::kRenderPlanSchemaV2);
    REQUIRE(plan.layers.size() == 2);
    CHECK_FALSE(plan.layers[1].parent.has_value());
    REQUIRE(plan.layers[1].animation.has_value());
    REQUIRE(plan.layers[1].animation->tracks.size() == 4);
    CHECK(plan.layers[1].position[0] == doctest::Approx(0.f));
    CHECK(plan.layers[1].position[1] == doctest::Approx(0.f));
    CHECK(plan.layers[1].animation->tracks[0].keyframes.size() == 31);
    CHECK(plan.layers[1].animation->tracks[0].keyframes.back().value[0] == doctest::Approx(30.f));
    CHECK(plan.layers[1].animation->tracks[0].keyframes.back().value[1] == doctest::Approx(20.f));
    CHECK(plan.layers[0].animation->tracks[0].keyframes.back().value[0] == doctest::Approx(0.f));
    CHECK_MESSAGE(canonicallyValid(plan), canonicalDiagnostic(plan));

    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    const auto v3 = lowerToRenderPlan(scene, options);
    CHECK(v3.schema == chronon3d::render_plan::kRenderPlanSchemaV3);
    CHECK(v3.layers[1].position[0] == doctest::Approx(0.f));
    CHECK(v3.layers[1].animation->tracks[0].keyframes.back().value[0] == doctest::Approx(30.f));
    CHECK_MESSAGE(canonicallyValid(v3), canonicalDiagnostic(v3));

    MotionScene cameraScene("camera adapter", 30.f);
    auto& cameraContent = cameraScene.addContent(21, "camera image", ContentRef::image("camera-image", {16.f, 16.f}));
    cameraContent.lifetime = FrameRange::between(0, 1);
    chrononmotion::motion::CameraRig rig(22, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    rig.positionTrack().add(0.f, Vector3(0.f, 0.f, -8.f));
    rig.positionTrack().add(3.f / 30.f, Vector3(1.f, 0.f, -8.f));
    RenderPlanLoweringOptions cameraOptions;
    cameraOptions.cameraRig = &rig;
    LayerPlan cameraImage;
    cameraImage.type = LayerType::Image;
    cameraImage.asset = "camera-image";
    cameraOptions.contentLayers.emplace("21", cameraImage);
    const auto cameraPlan = lowerToRenderPlan(cameraScene, cameraOptions);
    REQUIRE(cameraPlan.camera.has_value());
    REQUIRE(cameraPlan.camera_animation.has_value());
    CHECK(cameraPlan.camera_animation->size() == 7);
    CHECK(cameraPlan.camera->position[0] == doctest::Approx(0.f));
    CHECK(cameraPlan.camera_animation->front().keyframes.back().value.front() == doctest::Approx(1.f));
    CHECK(cameraPlan.canvas.duration.integral() == 4);
    CHECK_MESSAGE(canonicallyValid(cameraPlan), canonicalDiagnostic(cameraPlan));

    rig.setAperture(0.035f).setFocusDistance(425.f);
    const auto dofCameraPlan = lowerToRenderPlan(cameraScene, cameraOptions);
    REQUIRE(dofCameraPlan.camera.has_value());
    CHECK(dofCameraPlan.camera->dof_enabled);
    CHECK(dofCameraPlan.camera->dof_aperture == doctest::Approx(0.035f));
    // Camera sits at z=-8 looking -Z, so the focal distance 425 maps to world plane 433.
    CHECK(dofCameraPlan.camera->dof_focus_distance == doctest::Approx(433.f));
    CHECK_MESSAGE(canonicallyValid(dofCameraPlan), canonicalDiagnostic(dofCameraPlan));
    const Json emittedDof = Json::parse(renderPlanToJson(dofCameraPlan));
    REQUIRE(emittedDof["camera"].contains("dof"));
    CHECK(emittedDof["camera"]["dof"]["enabled"] == true);
    CHECK(emittedDof["camera"]["dof"]["aperture"] == doctest::Approx(0.035f));
    CHECK(emittedDof["camera"]["dof"]["focus_distance"] == doctest::Approx(433.f));
    const auto decodedDof = chronon3d::render_plan::decode_render_plan(emittedDof);
    REQUIRE(decodedDof);
    REQUIRE(decodedDof->camera.has_value());
    CHECK(decodedDof->camera->dof_enabled);
    CHECK(decodedDof->camera->dof_aperture == doctest::Approx(0.035f));
    CHECK(decodedDof->camera->dof_focus_distance == doctest::Approx(433.f));
    const auto cameraDescriptor = chronon3d::render_plan::detail::make_camera_descriptor(*decodedDof);
    REQUIRE(cameraDescriptor.has_value());
    CHECK(cameraDescriptor->base.dof.enabled);
    CHECK(cameraDescriptor->base.dof.aperture == doctest::Approx(0.035f));
    CHECK(cameraDescriptor->base.dof.focus_distance == doctest::Approx(433.f));
    CHECK(cameraDescriptor->base.dof.focus_z == doctest::Approx(433.f));
    CHECK(cameraDescriptor->base.dof.max_blur == doctest::Approx(24.f));

}

TEST_CASE("ChrononMotion baked SVG Merge and Morph operators satisfy V3 schema") {
    MotionScene scene("operator adapter", 30.f);
    scene.addContent(31, "compound", ContentRef::image("compound", {100.f, 100.f}));
    scene.evaluate(0);
    RenderPlanLoweringOptions options;
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    LayerPlan shape;
    shape.type = LayerType::Shape;
    shape.size = {100.f, 100.f};
    shape.size_dimensions = 2;
    chronon3d::render_plan::ShapeContentPlan path;
    path.type = chronon3d::render_plan::ShapeContentTypePlan::Path;
    path.fill_color = std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
    path.path = lowerBakedPath(parseSvgPath("M0 0 L10 0 Z"));
    shape.shape = path;
    const auto repeated = expandPathRepeater(parseSvgPath("M0 0 L10 0 Z"), RepeaterSpec{3, {12.f, 0.f}});
    applyBakedPathGeometry(shape, parseSvgPath("M0 0 L10 0 Z"), repeated);
    options.contentLayers.emplace("31", shape);
    const auto merged = lowerToRenderPlan(scene, options);
    REQUIRE(merged.layers[0].shape.has_value());
    REQUIRE(merged.layers[0].shape->operators.size() == 1);
    CHECK(merged.layers[0].shape->operators.front().paths.size() == 2);
    CHECK(merged.layers[0].shape->path.size() == 3);
    CHECK(merged.layers[0].shape->operators.front().paths.front().size() == 3);
    CHECK(merged.layers[0].shape->operators.front().paths.front()[1].point[0] == doctest::Approx(22.f));
    CHECK_FALSE(renderPlanEmissionProblem(merged).empty()); // Shape layer plans are outside this adapter's writer contract.

    const auto baked = bakeSvgPathMorph("M0 0 L10 0 Z", "M0 0 L20 0 Z", 0, 2);
    shape.shape->operators.clear();
    applyBakedPathMorph(shape, baked);
    options.contentLayers["31"] = shape;
    const auto morphed = lowerToRenderPlan(scene, options);
    REQUIRE(morphed.layers[0].shape->operators.size() == 1);
    CHECK(morphed.layers[0].shape->operators.front().kind == chronon3d::render_plan::PathOperatorKindPlan::Morph);
    REQUIRE(morphed.layers[0].shape->operators.front().morph_animation.has_value());
    CHECK(morphed.layers[0].shape->operators.front().morph_animation->keyframes.size() == 3);
    CHECK(morphed.layers[0].shape->path[1].point[0] == doctest::Approx(10.f));
    CHECK(morphed.layers[0].shape->operators.front().morph_target[1].point[0] == doctest::Approx(20.f));
    CHECK(morphed.layers[0].shape->operators.front().morph_animation->keyframes[0].value[0] == doctest::Approx(0.f));
    CHECK(morphed.layers[0].shape->operators.front().morph_animation->keyframes[1].value[0] == doctest::Approx(0.5f));
    CHECK(morphed.layers[0].shape->operators.front().morph_animation->keyframes[2].value[0] == doctest::Approx(1.f));
    CHECK_FALSE(renderPlanEmissionProblem(morphed).empty()); // Shape layer plans are outside this adapter's writer contract.
    // The Motion bridge deliberately refuses shape layers; decoder topology
    // validation is exercised by the native RenderPlan decoder suite.

    auto unsupportedLens = CameraPose{};
    unsupportedLens.fov = 50.f;
    unsupportedLens.focalLength = 50.f;
    CHECK_THROWS_AS(lowerCameraPose(unsupportedLens, 0.1f, 1000.f), std::invalid_argument);
    unsupportedLens.focalLength = 0.f;
    unsupportedLens.aperture = 2.8f;
    CHECK_THROWS_AS(lowerCameraPose(unsupportedLens, 0.1f, 1000.f), std::invalid_argument);
}

TEST_CASE("ChrononMotion baked SVG paths lower to a schema-valid V3 shape plan") {
    MotionScene scene("path", 30.f);
    auto& source = scene.addContent(5, "Path", ContentRef::image("opaque-shape", {100.f, 100.f}));
    scene.evaluate(0);
    RenderPlanLoweringOptions options;
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    LayerPlan shape;
    shape.type = LayerType::Shape;
    shape.size = {100.f, 100.f};
    shape.size_dimensions = 2;
    chronon3d::render_plan::ShapeContentPlan content;
    content.type = chronon3d::render_plan::ShapeContentTypePlan::Path;
    content.fill_color = std::array<float, 4>{1.f, 0.f, 0.f, 1.f};
    content.path = lowerBakedPath(parseSvgPath("M0 0 C1 0 2 1 3 1 Z"));
    shape.shape = content;
    options.contentLayers.emplace("5", shape);
    const auto plan = lowerToRenderPlan(scene, options);
    CHECK(plan.layers[0].position[0] == doctest::Approx(960.f));
    CHECK_FALSE(plan.layers[0].animation.has_value());
    REQUIRE(plan.layers[0].shape.has_value());
    CHECK(plan.layers[0].shape->path.size() == 3);
    // Plain V3 path shapes (no operators) are within the writer contract.
    CHECK(renderPlanEmissionProblem(plan).empty());
    const Json emittedPath = Json::parse(renderPlanToJson(plan));
    const auto decodedPath = chronon3d::render_plan::decode_render_plan(emittedPath);
    REQUIRE(decodedPath);
    REQUIRE(decodedPath->layers[0].shape.has_value());
    CHECK(decodedPath->layers[0].shape->path.size() == 3);
}

TEST_CASE("ChrononMotion lowering refuses transforms the plan cannot represent") {
    MotionScene scene("sheared", 30.f);
    auto& root = scene.addNull(1, "root");
    scene.addContent(2, "child", ContentRef::image("opaque", {20.f, 10.f}), 1);
    root.transform.scale.set(2.f, 1.f, 1.f);
    root.transform.rotation.z = 0.5f;
    auto& sibling = scene.addContent(4, "sibling", ContentRef::image("other", {12.f, 12.f}));
    (void)sibling;
    scene.findLayer(2)->transform.rotation.z = 0.5f;
    scene.findLayer(4)->transform.rotation.z = 0.f;
    scene.evaluate(0);
    RenderPlanLoweringOptions options;
    LayerPlan content;
    content.type = LayerType::Image;
    content.asset = "opaque";
    options.contentLayers.emplace("2", content);
    LayerPlan siblingPlan;
    siblingPlan.type = LayerType::Image;
    siblingPlan.asset = "other";
    options.contentLayers.emplace("4", siblingPlan);
    CHECK_THROWS_AS(lowerToRenderPlan(scene, options), std::invalid_argument);

    MotionScene anchorOffset("anchor", 30.f);
    auto& anchored = anchorOffset.addContent(9, "anchored", ContentRef::text("title", {100.f, 50.f}));
    anchored.transform.anchor.set(50.f, 25.f, 0.f);
    anchorOffset.evaluate(0);
    LayerPlan text;
    text.type = LayerType::Text;
    options.contentLayers.clear();
    options.contentLayers.emplace("9", text);
    CHECK_THROWS_AS(lowerToRenderPlan(anchorOffset, options), std::invalid_argument);

    MotionScene reflected("reflected", 30.f);
    auto& mirrored = reflected.addContent(3, "mirror", ContentRef::image("mirror-asset", {20.f, 10.f}));
    mirrored.transform.scale.x = -1.f;
    reflected.evaluate(0);
    LayerPlan mirrorContent;
    mirrorContent.type = LayerType::Image;
    options.contentLayers.clear();
    options.contentLayers.emplace("3", mirrorContent);
    CHECK_THROWS_AS(lowerToRenderPlan(reflected, options), std::invalid_argument);
}

TEST_CASE("ChrononMotion lowering preserves the caller's evaluated frame") {
    MotionScene scene("restore", 30.f);
    auto& layer = scene.addContent(12, "moving", ContentRef::image("asset", {20.f, 10.f}));
    layer.tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f));
    layer.tracks.position.add(1.f, Vector3(30.f, 0.f, 0.f));
    scene.evaluatePtsExact(0.6f);
    const Vector3 before = scene.state(12).local.position;
    const float beforePts = scene.lastEvaluatedPts();
    LayerPlan image;
    image.type = LayerType::Image;
    image.asset = "asset";
    RenderPlanLoweringOptions options;
    options.contentLayers.emplace("12", image);
    (void)lowerToRenderPlan(scene, options);
    CHECK(scene.lastEvaluatedFrame() == 18);
    CHECK(scene.lastEvaluatedPts() == doctest::Approx(beforePts));
    CHECK(scene.state(12).local.position == before);
}
