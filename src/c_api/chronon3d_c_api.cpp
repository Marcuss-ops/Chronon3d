#include <chronon3d/c_api/chronon3d.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
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
    chronon3d::assets::AssetResolver resolver;
    std::string last_error;
    std::uint8_t* buffer{nullptr};
    std::size_t buffer_size{0};

    explicit chronon_engine(const chronon3d::sdk::RenderSettings& settings)
        : engine(settings) {}
};

namespace {

std::shared_ptr<const chronon3d::Composition> compile_plan(
    const json& root, const chronon3d::assets::AssetResolver& resolver) {
    const auto decoded = chronon3d::render_plan::decode_render_plan(root);
    if (!decoded) throw std::runtime_error(decoded.error().message);
    const auto compiled = chronon3d::render_plan::compile_render_plan(
        decoded.value(), resolver);
    if (!compiled) throw std::runtime_error(compiled.error().message);
    return compiled.value();
}

chronon_status set_error(chronon_engine* engine, chronon_status status,
                         std::string message);

chronon_status set_error(chronon_engine* engine, chronon_status status,
                          std::string message) {
    if (engine) engine->last_error = std::move(message);
    return status;
}

} // namespace

extern "C" {

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
        if (config && config->assets_root) {
            engine->engine.set_assets_root(config->assets_root);
            engine->resolver.mount(config->assets_root);
        }
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
        plan->composition = compile_plan(root, engine->resolver);
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
