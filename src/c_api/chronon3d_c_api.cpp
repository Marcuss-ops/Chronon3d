#include <chronon3d/c_api/chronon3d.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/timeline/composition.hpp>
#include "chronon3d_version.hpp"

#include <nlohmann/json.hpp>
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

struct chronon_plan {
    chronon3d::render_plan::PreparedRenderPlan prepared;
    chronon3d::sdk::RenderSettings settings{};
};

struct chronon_engine {
    chronon3d::sdk::RenderEngine engine;
    chronon3d::assets::AssetResolver resolver;
    std::string last_error;
    chronon_status last_status{CHRONON_OK};
    std::string last_code;
    std::string last_component;
    std::string last_node_id;
    std::string last_asset;
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

template <typename Rendered>
std::optional<std::size_t> rendered_byte_size(const Rendered& rendered) {
    if (rendered.width <= 0 || rendered.height <= 0) return std::nullopt;
    const auto tight_row_bytes = static_cast<std::size_t>(rendered.width) * 4;
    const auto row_bytes = rendered.bytes_per_row == 0 ? tight_row_bytes : rendered.bytes_per_row;
    if (row_bytes > std::numeric_limits<std::size_t>::max() /
                        static_cast<std::size_t>(rendered.height)) {
        return std::nullopt;
    }
    return row_bytes * static_cast<std::size_t>(rendered.height);
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

chronon3d::render_plan::PreparedRenderPlan compile_plan(
    const json& root, chronon3d::assets::AssetResolver& resolver) {
    const auto decoded = chronon3d::render_plan::decode_render_plan(root);
    if (!decoded)
        throw PlanCompileError(decoded.error().path, decoded.error().message,
                               decoded.error().code, decoded.error().component);
    chronon3d::render_plan::RenderPlanFingerprintOptions fingerprint_options;
    fingerprint_options.render_settings.width = decoded->canvas.width;
    fingerprint_options.render_settings.height = decoded->canvas.height;
    fingerprint_options.render_settings.deterministic = false;
    fingerprint_options.render_settings.force_scalar_normal_blend = false;
    const auto compiled = chronon3d::render_plan::compile_render_plan(
        decoded.value(), resolver, fingerprint_options);
    if (!compiled)
        throw PlanCompileError(compiled.error().path, compiled.error().message,
                               compiled.error().code, compiled.error().component);
    return compiled.value();
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

chronon_status set_error(chronon_engine* engine, chronon_status status,
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

chronon_status set_render_error(chronon_engine* engine,
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

extern "C" {
#include "error_bridge.inc"
#include "engine_lifetime.inc"
#include "log_callback_bridge.inc"
#include "plan_compile_api.inc"
#include "render_api.inc"
#include "buffer_management.inc"
} // extern "C"
