#include <chronon/abi.h>
#include "chronon3d_c_api_internal.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/motion/contract_apply.hpp>
#include <chronon3d/motion/contract_bridge.hpp>
#include <chronon3d/motion/element_contract.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/timeline/composition.hpp>
#include "chronon3d_version.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using json = nlohmann::json;

using chronon_c_api_detail::EngineUseGuard;
using chronon_c_api_detail::rendered_byte_size;

namespace {

void log_engine_creation_failure(std::string_view detail) noexcept {
    try {
        spdlog::error("[chronon-c-api] engine creation failed: {}", detail);
    } catch (...) {
        // Diagnostics must not let an exception cross the C ABI boundary.
    }
}

void clear_error_impl(chronon_engine* engine) {
    if (!engine) return;
    engine->last_error.clear();
    engine->last_status = CHRONON_OK;
    engine->last_code.clear();
    engine->last_component.clear();
    engine->last_node_id.clear();
    engine->last_asset.clear();
}

void write_error_info(chronon_error_info* info, chronon_status status,
                      std::string message) {
    constexpr std::size_t base_size =
        offsetof(chronon_error_info, message) + sizeof(chronon_error_info::message);
    if (!info || info->struct_size < base_size) return;
    static thread_local std::string storage;
    static thread_local std::string code_storage;
    static thread_local std::string component_storage;
    storage = std::move(message);
    code_storage = chronon_status_name(status);
    component_storage = "c_api";
    info->status = status;
    info->message = storage.c_str();
    if (info->struct_size >= sizeof(chronon_error_info)) {
        info->code = code_storage.c_str();
        info->component = component_storage.c_str();
        info->node_id = nullptr;
        info->asset = nullptr;
    }
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

struct PlanCompileError : std::runtime_error {
    std::string path;
    std::string code;
    std::string component;
    PlanCompileError(std::string error_path, std::string message,
                     std::string error_code, std::string error_component)
        : std::runtime_error(std::move(message)),
          path(std::move(error_path)),
          code(std::move(error_code)),
          component(std::move(error_component)) {}
};

chronon3d::compiler::CompiledProgram compile_plan(
    const json& root, chronon3d::assets::AssetResolver& resolver,
    chronon3d::sdk::RenderSettings* plan_settings = nullptr) {
    const auto decoded = chronon3d::render_plan::decode_render_plan(root);
    if (!decoded)
        throw PlanCompileError(decoded.error().path, decoded.error().message,
                               decoded.error().code, decoded.error().component);
    chronon3d::render_plan::RenderPlanFingerprintOptions fingerprint_options;
    fingerprint_options.render_settings.width = decoded->canvas.width;
    fingerprint_options.render_settings.height = decoded->canvas.height;
    fingerprint_options.render_settings.motion_blur =
        decoded->temporal_motion_blur.has_value();
    fingerprint_options.render_settings.deterministic = false;
    fingerprint_options.render_settings.force_scalar_normal_blend = false;
    chronon3d::compiler::CompileOptions compile_options;
    compile_options.fingerprint = fingerprint_options;
    auto compiled = chronon3d::compiler::compile(
        decoded.value(), resolver, compile_options);
    if (!compiled) {
        const auto& error = compiled.error();
        const bool missing_asset =
            std::string_view(error.path).starts_with("assets.");
        throw PlanCompileError(compiled.error().path,
                               compiled.error().message,
                               missing_asset ? "MissingAsset" : "compile_error",
                               compiled.error().component);
    }
    if (plan_settings) {
        plan_settings->width = decoded->canvas.width;
        plan_settings->height = decoded->canvas.height;
        plan_settings->motion_blur = decoded->temporal_motion_blur.has_value();
        if (decoded->temporal_motion_blur) {
            plan_settings->motion_blur_samples = static_cast<int>(
                decoded->temporal_motion_blur->samples);
            plan_settings->motion_blur_shutter_angle_deg =
                decoded->temporal_motion_blur->shutter_angle;
            plan_settings->motion_blur_shutter_phase_deg = -90.0f;
        }
    }
    return std::move(compiled).value();
}

chronon_status render_error_status(const chronon3d::sdk::RenderError& error);

chronon_status store_error(chronon_engine* engine, chronon_status status,
                           std::string message, std::string code,
                           std::string component, std::string node_id,
                           std::string asset) {
    if (!engine) return status;
    engine->last_error = std::move(message);
    engine->last_status = status;
    engine->last_code = std::move(code);
    engine->last_component = std::move(component);
    engine->last_node_id = std::move(node_id);
    engine->last_asset = std::move(asset);
    return status;
}

chronon_status set_error_impl(chronon_engine* engine, chronon_status status,
                         std::string message) {
    return store_error(engine, status, std::move(message),
                       chronon_status_name(status), "c_api", {}, {});
}

chronon_status render_error_status(const chronon3d::sdk::RenderError& error) {
    if (error.code == chronon3d::sdk::RenderErrorCode::AssetChanged)
        return CHRONON_ERROR_ASSET_CHANGED;
    if (error.code == chronon3d::sdk::RenderErrorCode::Cancelled)
        return CHRONON_ERROR_CANCELLED;
    switch (error.code) {
        case chronon3d::sdk::RenderErrorCode::InvalidPlan: return CHRONON_ERROR_INVALID_PLAN;
        case chronon3d::sdk::RenderErrorCode::UnsupportedSchema: return CHRONON_ERROR_UNSUPPORTED_SCHEMA;
        case chronon3d::sdk::RenderErrorCode::AssetNotFound: return CHRONON_ERROR_ASSET_NOT_FOUND;
        case chronon3d::sdk::RenderErrorCode::DecodeFailure: return CHRONON_ERROR_DECODE_FAILED;
        case chronon3d::sdk::RenderErrorCode::EncodeFailure: return CHRONON_ERROR_ENCODE_FAILED;
        case chronon3d::sdk::RenderErrorCode::OutOfMemory: return CHRONON_ERROR_OUT_OF_MEMORY;
        case chronon3d::sdk::RenderErrorCode::AbiMismatch: return CHRONON_ERROR_ABI_MISMATCH;
        default: break;
    }
    return CHRONON_ERROR_RENDER_FAILED;
}

chronon_status set_render_error_impl(chronon_engine* engine,
                                const chronon3d::sdk::RenderError& error) {
    const chronon_status status = render_error_status(error);
    return store_error(engine, status, error.message,
                       chronon_status_name(status),
                       error.component, error.node_id, error.asset);
}

chronon_status plan_error_status(std::string_view code) {
    if (code == "MissingAsset") return CHRONON_ERROR_ASSET_NOT_FOUND;
    return CHRONON_ERROR_PARSE_FAILED;
}

chronon_status set_plan_error(chronon_engine* engine, std::string message,
                              std::string path, std::string code,
                              std::string component) {
    const chronon_status status = plan_error_status(code);
    if (component.empty()) component = "render_plan";
    std::string asset;
    std::string node_id;
    constexpr std::string_view kAssetsPrefix = "assets.";
    if (std::string_view(path).substr(0, kAssetsPrefix.size()) == kAssetsPrefix) {
        asset = path.substr(kAssetsPrefix.size());
    } else if (!path.empty()) {
        node_id = std::move(path);
    }
    return store_error(engine, status, std::move(message),
                       chronon_status_name(status),
                       std::move(component), std::move(node_id), std::move(asset));
}

} // namespace

namespace chronon_c_api_detail {
void clear_error(chronon_engine* engine) { clear_error_impl(engine); }
chronon_status set_error(chronon_engine* engine, chronon_status status, std::string message) {
    return set_error_impl(engine, status, std::move(message));
}
chronon_status set_render_error(chronon_engine* engine,
                                const chronon3d::sdk::RenderError& error) {
    return set_render_error_impl(engine, error);
}
} // namespace chronon_c_api_detail

using chronon_c_api_detail::clear_error;
using chronon_c_api_detail::set_error;
using chronon_c_api_detail::set_render_error;

extern "C" {
const char* chronon_version_string(void) {
    return CHRONON3D_PROJECT_VERSION_STRING;
}

const char* chronon_status_name(chronon_status status) {
    switch (status) {
        case CHRONON_OK: return "OK";
        case CHRONON_ERROR_UNKNOWN: return "UNKNOWN";
        case CHRONON_ERROR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case CHRONON_ERROR_PARSE_FAILED: return "PARSE_FAILED";
        case CHRONON_ERROR_RENDER_FAILED: return "RENDER_FAILED";
        case CHRONON_ERROR_IO_FAILED: return "IO_FAILED";
        case CHRONON_ERROR_UNSUPPORTED: return "UNSUPPORTED";
        case CHRONON_ERROR_CANCELLED: return "CANCELLED";
        case CHRONON_ERROR_ABI_MISMATCH: return "ABI_MISMATCH";
        case CHRONON_ERROR_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
        case CHRONON_ERROR_BUSY: return "BUSY";
        case CHRONON_ERROR_ASSET_CHANGED: return "ASSET_CHANGED";
        case CHRONON_ERROR_BUDGET_EXCEEDED: return "BUDGET_EXCEEDED";
        case CHRONON_ERROR_PREFLIGHT_FAILED: return "PREFLIGHT_FAILED";
        case CHRONON_ERROR_INVALID_PLAN: return "INVALID_PLAN";
        case CHRONON_ERROR_UNSUPPORTED_SCHEMA: return "UNSUPPORTED_SCHEMA";
        case CHRONON_ERROR_ASSET_NOT_FOUND: return "ASSET_NOT_FOUND";
        case CHRONON_ERROR_DECODE_FAILED: return "DECODE_FAILED";
        case CHRONON_ERROR_ENCODE_FAILED: return "ENCODE_FAILED";
        case CHRONON_ERROR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    }
    return "UNKNOWN";
}

uint32_t chronon_abi_version(void) { return CHRONON_ABI_VERSION; }

/* ADR-032 contract zone. Independent of the engine ABI: bumps only when a
 * contract-zone record (chronon_content_metrics / chronon_layer_state) or
 * the chronon_content_kind enum changes shape or value.
 *
 * The value has one source, chronon3d::motion::kContractZoneVersion, owned by
 * the contract bridge. On ELF the engine already defines the symbol
 * (src/scene/motion_contract_bridge.cpp), so this wrapper re-exports the
 * engine's definition rather than carrying a second copy. Windows keeps the
 * definition here, where CHRONON3D_BUILD_DLL turns abi.h's declaration into an
 * export instead of a dllimport. */
#if defined(_WIN32)
uint32_t chronon_contract_version(void) { return chronon3d::motion::kContractZoneVersion; }
#endif

const char* chronon_engine_last_error(chronon_engine* engine) {
    return engine ? engine->last_error.c_str() : "invalid engine";
}

chronon_status chronon_engine_last_error_info(chronon_engine* engine,
                                              chronon_error_info* out) {
    constexpr std::size_t base_size =
        offsetof(chronon_error_info, message) + sizeof(chronon_error_info::message);
    if (!engine || !out || out->struct_size < base_size)
        return CHRONON_ERROR_INVALID_ARGUMENT;
    out->status = engine->last_status;
    out->message = engine->last_error.empty() ? nullptr : engine->last_error.c_str();
    if (out->struct_size >= sizeof(chronon_error_info)) {
        out->code = engine->last_code.empty() ? nullptr : engine->last_code.c_str();
        out->component = engine->last_component.empty() ? nullptr : engine->last_component.c_str();
        out->node_id = engine->last_node_id.empty() ? nullptr : engine->last_node_id.c_str();
        out->asset = engine->last_asset.empty() ? nullptr : engine->last_asset.c_str();
    }
    return CHRONON_OK;
}
chronon_engine* chronon_engine_create(const chronon_engine_config* config) {
    try {
        std::string error;
        const auto status = validate_engine_config(config, error);
        if (status != CHRONON_OK) {
            log_engine_creation_failure(error);
            return nullptr;
        }
        chronon3d::sdk::RenderSettings settings;
        auto engine = std::make_unique<chronon_engine>(settings);
        if (config && config->assets_root) {
            engine->engine.set_assets_root(config->assets_root);
            engine->resolver.mount(config->assets_root);
        }
        return engine.release();
    } catch (const std::exception& error) {
        log_engine_creation_failure(error.what());
        return nullptr;
    } catch (...) {
        log_engine_creation_failure("unknown exception");
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
        write_error_info(out_error, CHRONON_ERROR_INVALID_ARGUMENT, "out_engine is null");
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
        auto engine = std::make_unique<chronon_engine>(settings);
        if (config && config->assets_root) {
            engine->engine.set_assets_root(config->assets_root);
            engine->resolver.mount(config->assets_root);
        }
        *out_engine = engine.release();
        return CHRONON_OK;
    } catch (const std::exception& error) {
        write_error_info(out_error, CHRONON_ERROR_UNKNOWN, error.what());
        return CHRONON_ERROR_UNKNOWN;
    } catch (...) {
        write_error_info(out_error, CHRONON_ERROR_UNKNOWN, "unknown engine creation failure");
        return CHRONON_ERROR_UNKNOWN;
    }
}

void chronon_engine_destroy(chronon_engine* engine) {
    if (!engine) return;
    std::free(engine->buffer);
    delete engine;
}
chronon_status chronon_engine_set_log_callback(
    chronon_engine* engine, chronon_log_callback callback, void* user) {
    if (!engine) return CHRONON_ERROR_INVALID_ARGUMENT;
    if (!callback) {
        engine->engine.set_log_callback({}, nullptr);
        return CHRONON_OK;
    }
    engine->engine.set_log_callback(
        [callback](chronon3d::sdk::LogLevel level,
                   std::string_view component,
                   std::string_view message, void* callback_user) {
            callback(static_cast<int>(level),
                     std::string(component).c_str(),
                     std::string(message).c_str(), callback_user);
        }, user);
    return CHRONON_OK;
}
chronon_status chronon_plan_compile_json_n(chronon_engine* engine, const char* source,
                                           uint64_t source_size,
                                           chronon_plan** out_plan) {
    if (!engine || !source || source_size == 0 || !out_plan)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid plan arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    *out_plan = nullptr;
    try {
        const auto root = json::parse(source, source + source_size);
        chronon3d::sdk::RenderSettings plan_settings;
        auto program = compile_plan(root, engine->resolver, &plan_settings);
        auto plan = std::make_unique<chronon_plan>(std::move(program));
        plan->settings = plan_settings;
        *out_plan = plan.release();
        clear_error(engine);
        return CHRONON_OK;
    } catch (const PlanCompileError& error) {
        return set_plan_error(engine, error.what(), error.path, error.code, error.component);
    } catch (const std::exception& error) {
        return set_error(engine, CHRONON_ERROR_PARSE_FAILED, error.what());
    }
}

chronon_status chronon_plan_compile_json(chronon_engine* engine, const char* source,
                                         chronon_plan** out_plan) {
    if (!source) return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid plan arguments");
    return chronon_plan_compile_json_n(engine, source, std::strlen(source), out_plan);
}

void chronon_plan_destroy(chronon_plan* plan) { delete plan; }
} // extern "C"
