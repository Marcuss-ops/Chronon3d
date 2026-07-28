#include <chronon3d/c_api/chronon3d.h>

#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/presets/text/subtitle.hpp>
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

using json = nlohmann::json;

struct chronon_plan {
    std::unique_ptr<chronon3d::Composition> composition;
};

struct chronon_engine {
    chronon3d::sdk::RenderEngine engine;
    std::string last_error;
    std::uint8_t* buffer{nullptr};
    std::size_t buffer_size{0};

    explicit chronon_engine(const chronon3d::sdk::RenderSettings& settings)
        : engine(settings) {}
};

struct chronon_context {
    std::unique_ptr<chronon_engine> engine;
};

namespace {

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read asset: " + path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

chronon3d::FitMode fit_mode(std::string_view raw) {
    if (raw == "contain") return chronon3d::FitMode::Contain;
    if (raw == "stretch") return chronon3d::FitMode::Stretch;
    if (raw == "none") return chronon3d::FitMode::None;
    return chronon3d::FitMode::Cover;
}

chronon3d::Color color_value(const json& value,
                             chronon3d::Color fallback = chronon3d::Color::white()) {
    if (!value.is_array() || value.size() < 3) return fallback;
    return {
        value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
        value.size() > 3 ? value[3].get<float>() : 1.0f};
}

chronon3d::TextDefinition make_text_definition(
    const json& layer, const chronon3d::CanvasInfo& canvas) {
    const std::string text = layer.value("text", std::string{});
    const auto preset = layer.value("preset", std::string{});
    if (preset == "title_centered")
        return chronon3d::presets::text::title_centered(text, canvas);
    if (preset == "subtitle_bottom")
        return chronon3d::presets::text::subtitle_bottom(text, canvas);
    if (preset == "caption_safe_area")
        return chronon3d::presets::text::caption_safe_area(text, canvas);
    if (preset == "kinetic_word")
        return chronon3d::presets::text::kinetic_word(text, canvas);
    if (preset == "lower_third")
        return chronon3d::presets::text::lower_third(text, canvas);

    chronon3d::TextDefinition def;
    def.content.value = text;
    def.style.font.font_path = layer.value("font", std::string{});
    def.style.font.font_size = layer.value("font_size", 48.0f);
    def.style.color = color_value(layer.value("color", json::array()));
    def.frame.size = {
        layer.value("box_width", static_cast<float>(canvas.width)),
        layer.value("box_height", static_cast<float>(canvas.height))};
    def.frame.align = chronon3d::TextAlign::Center;
    def.frame.vertical_align = chronon3d::VerticalAlign::Middle;
    return def;
}

void apply_layer_timing(chronon3d::LayerBuilder& builder, const json& layer) {
    if (layer.contains("start_frame"))
        builder.from(chronon3d::Frame{layer.at("start_frame").get<std::int64_t>()});
    if (layer.contains("duration_frames"))
        builder.duration(chronon3d::Frame{layer.at("duration_frames").get<std::int64_t>()});
    if (layer.contains("position") && layer.at("position").is_array()) {
        const auto& p = layer.at("position");
        if (p.size() >= 2)
            builder.position({p[0].get<float>(), p[1].get<float>(),
                              p.size() > 2 ? p[2].get<float>() : 0.0f});
    }
    if (layer.contains("animation")) {
        const auto& animation = layer.at("animation");
        const auto preset = animation.value("preset", std::string{});
        if (!preset.empty()) builder.motion(preset);
    }
}

std::unique_ptr<chronon3d::Composition> compile_plan(const json& root) {
    // TICKET-JSON-SCHEMA-VALIDATOR — replace the manual two-field
    // check (`schema` + `version`) with a real validator that walks the
    // full `chronon.render-plan.v1` schema.  The validator accumulates
    // ALL issues (not fail-fast) so the user sees the complete diff in
    // one error message.  Throws std::runtime_error on any issue so the
    // C API entry points can map it to CHRONON_ERROR_PARSE_FAILED via
    // the existing try/catch.
    chronon3d::render_plan::validate_render_plan_or_throw(root);

    const auto canvas_json = root.at("canvas");
    const int width = canvas_json.value("width", 1920);
    const int height = canvas_json.value("height", 1080);
    const int fps = canvas_json.value("fps", 30);
    const auto duration = canvas_json.value("duration_frames", 0LL);
    const auto layers = root.value("layers", json::array());
    const auto canvas = chronon3d::CanvasInfo::from_dimensions(
        static_cast<float>(width), static_cast<float>(height));

    chronon3d::CompositionSpec spec;
    spec.name = root.value("job_id", std::string{"chronon_plan"});
    spec.width = width;
    spec.height = height;
    spec.frame_rate = {fps, 1};
    spec.duration = chronon3d::Frame{duration};
    spec.assets_root = root.value("assets_root", std::string{});

    auto composition = std::make_unique<chronon3d::Composition>(
        std::move(spec), [layers, canvas, width, height](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx);
            if (ctx.runtime) scene.font_engine(&ctx.runtime->font_engine());
            for (const auto& layer : layers) {
                const auto id = layer.value("id", std::string{"layer"});
                const auto type = layer.value("type", std::string{});
                scene.layer(id, [&](chronon3d::LayerBuilder& builder) {
                    if (type == "image") {
                        chronon3d::ImageParams params;
                        params.asset_path = layer.value("asset", std::string{});
                        params.size = {layer.value("width", static_cast<float>(width)),
                                       layer.value("height", static_cast<float>(height))};
                        params.fit = fit_mode(layer.value("fit", std::string{"cover"}));
                        builder.image("image", std::move(params));
                    } else if (type == "video") {
                        builder.video(layer.value("source", std::string{}));
                    } else if (type == "color") {
                        chronon3d::RectParams params;
                        params.size = {static_cast<float>(width), static_cast<float>(height)};
                        params.color = color_value(layer.value("color", json::array()),
                                                   chronon3d::Color::black());
                        builder.rect("color", std::move(params));
                    } else if (type == "text") {
                        builder.kind(chronon3d::LayerKind::Text);
                        builder.text("text", make_text_definition(layer, canvas));
                    } else if (type == "subtitle_track") {
                        const auto source = layer.value("source", std::string{});
                        const auto raw = source.find("WEBVTT") == 0
                            ? read_text_file(source)
                            : read_text_file(source);
                        chronon3d::presets::text::SubtitleTrack track;
                        const auto format = layer.value("format", std::string{"srt"});
                        if (format == "vtt") track = chronon3d::presets::text::subtitle_from_vtt(raw);
                        else if (format == "json") track = chronon3d::presets::text::subtitle_from_json(raw);
                        else track = chronon3d::presets::text::subtitle_from_srt(raw);
                        chronon3d::authoring::Layer authoring_layer(builder, canvas);
                        auto subtitles = authoring_layer.subtitles(track);
                        subtitles.preset(layer.value("preset", std::string{"minimal_white"}))
                            .font(layer.value("font", std::string{}), layer.value("font_size", 48.0f))
                            .build();
                    } else {
                        throw std::runtime_error("unsupported layer type: " + type);
                    }
                    apply_layer_timing(builder, layer);
                });
            }
            return scene.build();
        });
    return composition;
}

// TICKET-JSON-SCHEMA-VALIDATOR contract — `legacy_scene_to_plan` MUST
// produce a plan conformant to `schemas/chronon.render-plan.v1.schema.json`.
// The validator at `render_legacy_json()` exit enforces this contract
// (fail-loud on any drift).  When extending the legacy scene format
// (e.g. adding a new optional top-level field to the synthesized plan),
// update the inlined schema in `src/render_plan/render_plan_validator.cpp`
// AND the canonical schema file in lockstep.
json legacy_scene_to_plan(const json& scene, const chronon_render_options* options) {
    json plan;
    plan["schema"] = "chronon.render-plan";
    plan["version"] = 1;
    plan["job_id"] = scene.value("name", std::string{"legacy_scene"});
    plan["canvas"] = {
        {"width", options && options->width ? options->width : scene.value("width", 1920)},
        {"height", options && options->height ? options->height : scene.value("height", 1080)},
        {"fps", options && options->fps ? options->fps : 30},
        {"duration_frames", scene.value("duration", 1)}};
    plan["layers"] = json::array();
    for (const auto& layer : scene.value("layers", json::array())) {
        for (const auto& visual : layer.value("visuals", json::array())) {
            if (visual.value("type", std::string{}) != "rect")
                throw std::runtime_error("legacy C API supports only rect visuals");
            json output_layer = {
                {"id", layer.value("id", std::string{"layer"})},
                {"type", "color"},
                {"color", visual.value("color", json::array({1.0, 1.0, 1.0, 1.0}))}
            };
            if (visual.contains("pos")) output_layer["position"] = visual["pos"];
            plan["layers"].push_back(std::move(output_layer));
        }
    }
    plan["output"] = {{"path", "legacy.png"}, {"format", "png"}};
    return plan;
}

chronon_status set_error(chronon_engine* engine, chronon_status status,
                         std::string message);

chronon_status render_legacy_json(chronon_context* context, const char* source,
                                  const char* output_path,
                                  const chronon_render_options* options) {
    if (!context || !context->engine || !source || !output_path)
        return set_error(context ? context->engine.get() : nullptr,
                         CHRONON_ERROR_INVALID_ARGUMENT, "invalid legacy render arguments");
    try {
        auto root = json::parse(source);
        // Legacy scenes lack the `schema` field; convert FIRST so the
        // synthesized plan has the canonical shape, THEN validate.  The
        // `legacy_scene_to_plan` output is already conformant to the
        // schema, so validation acts as a smoke-test that the converter
        // did not silently drop a required field.  If a future change
        // extends the legacy converter (e.g. a new optional top-level
        // field), the validator's `additionalProperties: false` will
        // catch any drift at the conversion boundary.
        if (root.value("schema", std::string{}) != "chronon.render-plan")
            root = legacy_scene_to_plan(root, options);
        // TICKET-JSON-SCHEMA-VALIDATOR — validate at the legacy entry
        // point too, so legacy callers get fail-loud on schema drift
        // (e.g. type mismatch in the synthesized canvas).  This is a
        // no-op for clean legacy scenes; the legacy converter output
        // passes by construction.
        chronon3d::render_plan::validate_render_plan_or_throw(root);
        chronon_plan* plan = nullptr;
        auto status = chronon_plan_compile_json(context->engine.get(), root.dump().c_str(), &plan);
        if (status != CHRONON_OK) return status;
        const auto frame = options ? options->frame : 0;
        chronon_frame_buffer buffer{};
        status = chronon_render_frame(context->engine.get(), plan, frame, &buffer);
        if (status == CHRONON_OK && !stbi_write_png(output_path,
                                                     static_cast<int>(buffer.width),
                                                     static_cast<int>(buffer.height), 4,
                                                     buffer.data, static_cast<int>(buffer.stride))) {
            status = set_error(context->engine.get(), CHRONON_ERROR_IO_FAILED,
                               "PNG encoder failed");
        }
        chronon_plan_destroy(plan);
        return status;
    } catch (const std::exception& error) {
        return set_error(context->engine.get(), CHRONON_ERROR_PARSE_FAILED, error.what());
    }
}

chronon_status set_error(chronon_engine* engine, chronon_status status,
                          std::string message) {
    if (engine) engine->last_error = std::move(message);
    return status;
}

} // namespace

extern "C" {

chronon_context* chronon_create_context(void) {
    auto context = std::make_unique<chronon_context>();
    context->engine = std::unique_ptr<chronon_engine>(chronon_engine_create(nullptr));
    return context->engine ? context.release() : nullptr;
}

void chronon_destroy_context(chronon_context* context) {
    delete context;
}

chronon_status chronon_render_json_file(chronon_context* context, const char* json_path,
                                        const char* output_png_path,
                                        const chronon_render_options* options) {
    if (!json_path) return CHRONON_ERROR_INVALID_ARGUMENT;
    try {
        const auto source = read_text_file(json_path);
        return render_legacy_json(context, source.c_str(), output_png_path, options);
    } catch (const std::exception& error) {
        return set_error(context ? context->engine.get() : nullptr,
                         CHRONON_ERROR_IO_FAILED, error.what());
    }
}

chronon_status chronon_render_json_string(chronon_context* context, const char* json_string,
                                          const char* output_png_path,
                                          const chronon_render_options* options) {
    return render_legacy_json(context, json_string, output_png_path, options);
}

const char* chronon_last_error(chronon_context* context) {
    return context && context->engine ? chronon_engine_last_error(context->engine.get())
                                      : "invalid context";
}

const char* chronon_version_string(void) {
    return "0.1.0-alpha.1";
}

uint32_t chronon_abi_version(void) { return 1; }

chronon_engine* chronon_engine_create(const chronon_engine_config* config) {
    try {
        chronon3d::sdk::RenderSettings settings;
        if (config && config->struct_size >= sizeof(chronon_engine_config) && config->assets_root) {
            // The root is mounted below, after construction, to preserve the
            // SDK's per-engine asset isolation contract.
        }
        auto* engine = new chronon_engine(settings);
        if (config && config->struct_size >= sizeof(chronon_engine_config) && config->assets_root)
            engine->engine.set_assets_root(config->assets_root);
        return engine;
    } catch (...) {
        return nullptr;
    }
}

void chronon_engine_destroy(chronon_engine* engine) {
    if (!engine) return;
    std::free(engine->buffer);
    delete engine;
}

const char* chronon_engine_last_error(chronon_engine* engine) {
    return engine ? engine->last_error.c_str() : "invalid engine";
}

chronon_status chronon_plan_compile_json(chronon_engine* engine, const char* source,
                                         chronon_plan** out_plan) {
    if (!engine || !source || !out_plan)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid plan arguments");
    *out_plan = nullptr;
    try {
        const auto root = json::parse(source);
        auto plan = std::make_unique<chronon_plan>();
        plan->composition = compile_plan(root);
        chronon3d::sdk::RenderSettings settings;
        settings.width = root.at("canvas").value("width", 1920);
        settings.height = root.at("canvas").value("height", 1080);
        engine->engine.set_settings(settings);
        *out_plan = plan.release();
        return CHRONON_OK;
    } catch (const std::exception& error) {
        return set_error(engine, CHRONON_ERROR_PARSE_FAILED, error.what());
    }
}

void chronon_plan_destroy(chronon_plan* plan) { delete plan; }

chronon_status chronon_render_frame(chronon_engine* engine, const chronon_plan* plan,
                                    uint64_t frame, chronon_frame_buffer* output) {
    if (!engine || !plan || !plan->composition || !output)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid render arguments");
    std::memset(output, 0, sizeof(*output));
    auto result = engine->engine.render(*plan->composition,
                                        chronon3d::sdk::Frame{static_cast<std::int64_t>(frame)});
    if (!result) return set_error(engine, CHRONON_ERROR_RENDER_FAILED, result.error().message);
    const auto& rendered = result.value();
    const auto size = static_cast<std::size_t>(rendered.width) * rendered.height * 4;
    auto* data = static_cast<std::uint8_t*>(std::realloc(engine->buffer, size));
    if (!data) return set_error(engine, CHRONON_ERROR_IO_FAILED, "frame buffer allocation failed");
    engine->buffer = data;
    engine->buffer_size = size;
    std::memcpy(engine->buffer, rendered.pixels, size);
    output->data = engine->buffer;
    output->size = size;
    output->width = static_cast<uint32_t>(rendered.width);
    output->height = static_cast<uint32_t>(rendered.height);
    output->stride = static_cast<uint32_t>(rendered.bytes_per_row);
    output->pixel_format = static_cast<uint32_t>(rendered.format);
    return CHRONON_OK;
}

chronon_status chronon_render_file(chronon_engine* engine, const chronon_plan* plan,
                                   const char* output_path, uint64_t start_frame,
                                   uint64_t end_frame, uint32_t fps_num,
                                   uint32_t fps_den, const chronon_render_callbacks* cb) {
    if (!engine || !plan || !plan->composition || !output_path || fps_num == 0 || fps_den == 0)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid file render arguments");
    chronon3d::sdk::RenderFileRequest request;
    request.composition = plan->composition.get();
    request.output_path = output_path;
    request.start_frame = {static_cast<std::int64_t>(start_frame)};
    request.end_frame = {static_cast<std::int64_t>(end_frame)};
    request.frame_rate = {static_cast<std::int32_t>(fps_num), static_cast<std::int32_t>(fps_den)};
    chronon3d::sdk::RenderCallbacks callbacks;
    if (cb && cb->progress) callbacks.progress = [cb](auto current, auto total) {
        cb->progress(cb->user, static_cast<uint64_t>(current.integral()),
                     static_cast<uint64_t>(total.integral()));
    };
    if (cb && cb->is_cancelled) callbacks.is_cancelled = [cb] { return cb->is_cancelled(cb->user) != 0; };
    auto result = engine->engine.render_to_file(request, callbacks);
    if (!result) {
        return set_error(engine,
            result.error().code == chronon3d::sdk::RenderErrorCode::Cancelled
                ? CHRONON_ERROR_CANCELLED : CHRONON_ERROR_RENDER_FAILED,
            result.error().message);
    }
    return CHRONON_OK;
}

void chronon_buffer_free(chronon_engine* engine, chronon_frame_buffer* buffer) {
    if (!engine || !buffer) return;
    std::free(engine->buffer);
    engine->buffer = nullptr;
    engine->buffer_size = 0;
    std::memset(buffer, 0, sizeof(*buffer));
}

} // extern "C"
