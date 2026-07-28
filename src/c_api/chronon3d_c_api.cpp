#include <chronon3d/c_api/chronon3d.h>

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

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
    std::shared_ptr<const chronon3d::Composition> composition;
    chronon3d::sdk::RenderSettings settings{};
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

std::shared_ptr<const chronon3d::Composition> compile_plan(const json& root) {
    const auto decoded = chronon3d::render_plan::decode_render_plan(root);
    if (!decoded) throw std::runtime_error(decoded.error().message);
    const auto compiled = chronon3d::render_plan::compile_render_plan(decoded.value());
    if (!compiled) throw std::runtime_error(compiled.error().message);
    return compiled.value();
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
        if (config) {
            if (config->struct_size < sizeof(uint32_t) * 2)
                return nullptr;
            if (config->abi_version != chronon_abi_version())
                return nullptr;
            if (config->struct_size < sizeof(chronon_engine_config))
                return nullptr;
            if (config->flags != 0)
                return nullptr;
        }
        chronon3d::sdk::RenderSettings settings;
        auto* engine = new chronon_engine(settings);
        if (config && config->assets_root)
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
        plan->settings.width = root.at("canvas").at("width").get<int>();
        plan->settings.height = root.at("canvas").at("height").get<int>();
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
    engine->engine.set_settings(plan->settings);
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
    engine->engine.set_settings(plan->settings);
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
