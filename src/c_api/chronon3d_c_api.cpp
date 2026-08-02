#include <chronon3d/c_api/chronon3d.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/timeline/composition.hpp>
#include "chronon3d_version.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <atomic>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using json = nlohmann::json;

struct chronon_plan {
    chronon3d::render_plan::PreparedRenderPlan prepared;
    chronon3d::sdk::RenderSettings settings{};
};

struct chronon_engine {
    chronon3d::sdk::RenderEngine engine;
    chronon3d::assets::AssetResolver resolver;
    std::string last_error;
    std::uint8_t* buffer{nullptr};
    std::size_t buffer_size{0};
    std::atomic_flag in_use = ATOMIC_FLAG_INIT;

    explicit chronon_engine(const chronon3d::sdk::RenderSettings& settings)
        : engine(settings) {}
};

class EngineUseGuard {
public:
    explicit EngineUseGuard(std::atomic_flag& flag) : flag_(flag) {
        acquired_ = !flag_.test_and_set(std::memory_order_acquire);
    }
    ~EngineUseGuard() {
        if (acquired_) flag_.clear(std::memory_order_release);
    }
    EngineUseGuard(const EngineUseGuard&) = delete;
    EngineUseGuard& operator=(const EngineUseGuard&) = delete;
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }

private:
    std::atomic_flag& flag_;
    bool acquired_{false};
};

namespace {

void clear_error(chronon_engine* engine) {
    if (engine) engine->last_error.clear();
}

void write_error_info(chronon_error_info* info, chronon_status status,
                      std::string message) {
    if (!info || info->struct_size < sizeof(chronon_error_info)) return;
    static thread_local std::string storage;
    storage = std::move(message);
    info->status = status;
    info->message = storage.c_str();
}

chronon_status validate_engine_config(const chronon_engine_config* config,
                                      std::string& message) {
    if (!config) return CHRONON_OK;
    if (config->struct_size < sizeof(uint32_t) * 2) {
        message = "engine config is smaller than its ABI/version prefix";
        return CHRONON_ERROR_INVALID_ARGUMENT;
    }
    if (config->abi_version != chronon_abi_version()) {
        message = "engine config ABI version mismatch";
        return CHRONON_ERROR_ABI_MISMATCH;
    }
    if (config->struct_size < sizeof(chronon_engine_config)) {
        message = "engine config is smaller than the supported structure";
        return CHRONON_ERROR_INVALID_ARGUMENT;
    }
    if (config->flags != 0) {
        message = "engine config contains unsupported flags";
        return CHRONON_ERROR_UNSUPPORTED;
    }
    return CHRONON_OK;
}

template <typename Rendered>
std::optional<std::size_t> rendered_byte_size(const Rendered& rendered) {
    if (rendered.width <= 0 || rendered.height <= 0) return std::nullopt;
    const auto tight_row_bytes = static_cast<std::size_t>(rendered.width) * 4;
    const auto row_bytes = rendered.bytes_per_row == 0
        ? tight_row_bytes : rendered.bytes_per_row;
    if (row_bytes > std::numeric_limits<std::size_t>::max()
                       / static_cast<std::size_t>(rendered.height)) {
        return std::nullopt;
    }
    return row_bytes * static_cast<std::size_t>(rendered.height);
}

chronon3d::render_plan::PreparedRenderPlan compile_plan(
    const json& root, chronon3d::assets::AssetResolver& resolver) {
    const auto decoded = chronon3d::render_plan::decode_render_plan(root);
    if (!decoded) throw std::runtime_error(decoded.error().message);
    chronon3d::render_plan::RenderPlanFingerprintOptions fingerprint_options;
    fingerprint_options.render_settings.width = decoded->canvas.width;
    fingerprint_options.render_settings.height = decoded->canvas.height;
    fingerprint_options.render_settings.deterministic = false;
    fingerprint_options.render_settings.force_scalar_normal_blend = false;
    const auto compiled = chronon3d::render_plan::compile_render_plan(
        decoded.value(), resolver, fingerprint_options);
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
    return CHRONON3D_PROJECT_VERSION_STRING;
}

uint32_t chronon_abi_version(void) { return 1; }

chronon_engine* chronon_engine_create(const chronon_engine_config* config) {
    try {
        std::string error;
        if (validate_engine_config(config, error) != CHRONON_OK) return nullptr;
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

chronon_status chronon_engine_create_v2(const chronon_engine_config* config,
                                        chronon_engine** out_engine,
                                        chronon_error_info* out_error) {
    if (out_error && out_error->struct_size >= sizeof(chronon_error_info)) {
        out_error->status = CHRONON_OK;
        out_error->message = nullptr;
    }
    if (!out_engine) {
        write_error_info(out_error, CHRONON_ERROR_INVALID_ARGUMENT,
                         "out_engine is null");
        return CHRONON_ERROR_INVALID_ARGUMENT;
    }
    *out_engine = nullptr;
    try {
        std::string error;
        const auto status = validate_engine_config(config, error);
        if (status != CHRONON_OK) {
            write_error_info(out_error, status, std::move(error));
            return status;
        }
        chronon3d::sdk::RenderSettings settings;
        auto* engine = new chronon_engine(settings);
        if (config && config->assets_root) {
            engine->engine.set_assets_root(config->assets_root);
            engine->resolver.mount(config->assets_root);
        }
        *out_engine = engine;
        return CHRONON_OK;
    } catch (const std::exception& error) {
        write_error_info(out_error, CHRONON_ERROR_UNKNOWN, error.what());
        return CHRONON_ERROR_UNKNOWN;
    } catch (...) {
        write_error_info(out_error, CHRONON_ERROR_UNKNOWN,
                         "unknown engine creation failure");
        return CHRONON_ERROR_UNKNOWN;
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

chronon_status chronon_plan_compile_json_n(chronon_engine* engine, const char* source,
                                           uint64_t source_size,
                                           chronon_plan** out_plan) {
    if (!engine || !source || source_size == 0 || !out_plan)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid plan arguments");
    *out_plan = nullptr;
    try {
        const auto root = json::parse(source, source + source_size);
        auto plan = std::make_unique<chronon_plan>();
        plan->prepared = compile_plan(root, engine->resolver);
        plan->settings.width = plan->prepared.canvas.width;
        plan->settings.height = plan->prepared.canvas.height;
        *out_plan = plan.release();
        clear_error(engine);
        return CHRONON_OK;
    } catch (const std::exception& error) {
        return set_error(engine, CHRONON_ERROR_PARSE_FAILED, error.what());
    }
}

chronon_status chronon_plan_compile_json(chronon_engine* engine, const char* source,
                                         chronon_plan** out_plan) {
    if (!source) return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT,
                                  "invalid plan arguments");
    return chronon_plan_compile_json_n(engine, source, std::strlen(source), out_plan);
}

void chronon_plan_destroy(chronon_plan* plan) { delete plan; }

chronon_status chronon_render_frame(chronon_engine* engine, const chronon_plan* plan,
                                    uint64_t frame, chronon_frame_buffer* output) {
    if (!engine || !plan || !plan->prepared.composition || !output)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid render arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired())
        return set_error(engine, CHRONON_ERROR_BUSY, "engine is already rendering");
    std::memset(output, 0, sizeof(*output));
    engine->engine.set_settings(plan->settings);
    auto result = engine->engine.render(*plan->prepared.composition,
                                        chronon3d::sdk::Frame{static_cast<std::int64_t>(frame)});
    if (!result) return set_error(engine, CHRONON_ERROR_RENDER_FAILED, result.error().message);
    const auto& rendered = result.value();
    const auto size = rendered_byte_size(rendered);
    if (!size) return set_error(engine, CHRONON_ERROR_IO_FAILED,
                                "rendered frame size overflow or invalid dimensions");
    auto* data = static_cast<std::uint8_t*>(std::realloc(engine->buffer, *size));
    if (!data) return set_error(engine, CHRONON_ERROR_IO_FAILED, "frame buffer allocation failed");
    engine->buffer = data;
    engine->buffer_size = *size;
    std::memcpy(engine->buffer, rendered.pixels, *size);
    output->data = engine->buffer;
    output->size = *size;
    output->width = static_cast<uint32_t>(rendered.width);
    output->height = static_cast<uint32_t>(rendered.height);
    output->stride = static_cast<uint32_t>(rendered.bytes_per_row);
    output->pixel_format = static_cast<uint32_t>(rendered.format);
    clear_error(engine);
    return CHRONON_OK;
}

chronon_status chronon_render_frame_into(chronon_engine* engine,
                                          const chronon_plan* plan,
                                          uint64_t frame, void* destination,
                                          uint64_t destination_size,
                                          chronon_frame_info* output) {
    if (!engine || !plan || !plan->prepared.composition || !output)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT,
                         "invalid render-into arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired())
        return set_error(engine, CHRONON_ERROR_BUSY, "engine is already rendering");
    std::memset(output, 0, sizeof(*output));
    engine->engine.set_settings(plan->settings);
    auto result = engine->engine.render(
        *plan->prepared.composition,
        chronon3d::sdk::Frame{static_cast<std::int64_t>(frame)});
    if (!result)
        return set_error(engine, CHRONON_ERROR_RENDER_FAILED,
                         result.error().message);
    const auto& rendered = result.value();
    const auto size = rendered_byte_size(rendered);
    if (!size) return set_error(engine, CHRONON_ERROR_IO_FAILED,
                                "rendered frame size overflow or invalid dimensions");
    output->size = static_cast<uint64_t>(*size);
    if (!destination || destination_size < *size)
        return set_error(engine, CHRONON_ERROR_BUFFER_TOO_SMALL,
                         "destination buffer is too small; query required size in out_info->size");
    std::memcpy(destination, rendered.pixels, *size);
    output->width = static_cast<uint32_t>(rendered.width);
    output->height = static_cast<uint32_t>(rendered.height);
    output->stride = static_cast<uint32_t>(rendered.bytes_per_row);
    output->pixel_format = static_cast<uint32_t>(rendered.format);
    clear_error(engine);
    return CHRONON_OK;
}

chronon_status chronon_render_file(chronon_engine* engine, const chronon_plan* plan,
                                   const char* output_path, uint64_t start_frame,
                                   uint64_t end_frame, uint32_t fps_num,
                                   uint32_t fps_den, const chronon_render_callbacks* cb) {
    if (!engine || !plan || !plan->prepared.composition || !output_path || fps_num == 0 || fps_den == 0)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid file render arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired())
        return set_error(engine, CHRONON_ERROR_BUSY, "engine is already rendering");
    engine->engine.set_settings(plan->settings);
    chronon3d::sdk::RenderFileRequest request;
    request.composition = plan->prepared.composition.get();
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
    clear_error(engine);
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
