#include <doctest/doctest.h>

#include <chrononmotion/motion/OfflineBaking.hpp>
#include <chrononmotion/motion/RenderPlanLowering.hpp>
#include <chrononmotion/motion/MotionScene.hpp>

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::RenderPlan;
using Json = nlohmann::json;

namespace {

Json jsonForValidation(const RenderPlan& plan) {
    Json root{{"schema", plan.schema}, {"version", plan.schema == chronon3d::render_plan::kRenderPlanSchemaV3 ? 3 : 2},
              {"job_id", plan.job_id},
              {"canvas", {{"width", plan.canvas.width}, {"height", plan.canvas.height},
                           {"fps_num", plan.canvas.fps.numerator}, {"fps_den", plan.canvas.fps.denominator},
                           {"duration_frames", plan.canvas.duration.integral()}}},
              {"output", {{"path", plan.output.path}, {"format", "png"}}},
              {"layers", Json::array()}};
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
            Json shape{{"type", shapeType}, {"path", Json::array()}};
            if (layer.shape->fill_color) shape["fill"] = *layer.shape->fill_color;
            for (const auto& command : layer.shape->path) {
                Json encoded;
                switch (command.type) {
                    case chronon3d::render_plan::ShapePathCommandTypePlan::MoveTo:
                        encoded = {{"type", "move_to"}, {"point", command.point}}; break;
                    case chronon3d::render_plan::ShapePathCommandTypePlan::LineTo:
                        encoded = {{"type", "line_to"}, {"point", command.point}}; break;
                    case chronon3d::render_plan::ShapePathCommandTypePlan::QuadraticTo:
                        encoded = {{"type", "quadratic_to"}, {"point", command.point},
                                   {"control1", command.control1}}; break;
                    case chronon3d::render_plan::ShapePathCommandTypePlan::CubicTo:
                        encoded = {{"type", "cubic_to"}, {"point", command.point},
                                   {"control1", command.control1}, {"control2", command.control2}}; break;
                    case chronon3d::render_plan::ShapePathCommandTypePlan::Close:
                        encoded = {{"type", "close"}}; break;
                }
                shape["path"].push_back(std::move(encoded));
            }
            value["shape"] = std::move(shape);
        }
        root["layers"].push_back(std::move(value));
    }
    return root;
}

} // namespace

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
    CHECK(chronon3d::render_plan::validate_render_plan(jsonForValidation(plan)).ok());

    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    const auto v3 = lowerToRenderPlan(scene, options);
    CHECK(v3.schema == chronon3d::render_plan::kRenderPlanSchemaV3);
    CHECK(v3.layers[1].position[0] == doctest::Approx(0.f));
    CHECK(v3.layers[1].animation->tracks[0].keyframes.back().value[0] == doctest::Approx(30.f));
    CHECK(chronon3d::render_plan::validate_render_plan(jsonForValidation(v3)).ok());
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
    CHECK(chronon3d::render_plan::validate_render_plan(jsonForValidation(plan)).ok());
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
