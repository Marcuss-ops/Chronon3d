#pragma once

#include <chronon/abi.h>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/sdk/render_engine.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

struct chronon_plan {
    chronon3d::compiler::CompiledProgram program;
    chronon3d::sdk::RenderSettings settings{};
    explicit chronon_plan(chronon3d::compiler::CompiledProgram value)
        : program(std::move(value)) {}
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

namespace chronon_c_api_detail {
class EngineUseGuard {
public:
    explicit EngineUseGuard(std::atomic_flag& flag) : flag_(flag) {
        acquired_ = !flag_.test_and_set(std::memory_order_acquire);
    }
    ~EngineUseGuard() { if (acquired_) flag_.clear(std::memory_order_release); }
    EngineUseGuard(const EngineUseGuard&) = delete;
    EngineUseGuard& operator=(const EngineUseGuard&) = delete;
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }
private:
    std::atomic_flag& flag_;
    bool acquired_{false};
};

template <typename Rendered>
std::optional<std::size_t> rendered_byte_size(const Rendered& rendered) {
    if (rendered.width <= 0 || rendered.height <= 0) return std::nullopt;
    const auto tight_row_bytes = static_cast<std::size_t>(rendered.width) * 4;
    const auto row_bytes = rendered.bytes_per_row == 0 ? tight_row_bytes : rendered.bytes_per_row;
    if (row_bytes > std::numeric_limits<std::size_t>::max() /
                        static_cast<std::size_t>(rendered.height)) return std::nullopt;
    return row_bytes * static_cast<std::size_t>(rendered.height);
}

void clear_error(chronon_engine* engine);
chronon_status set_error(chronon_engine* engine, chronon_status status, std::string message);
chronon_status set_render_error(chronon_engine* engine,
                                const chronon3d::sdk::RenderError& error);
} // namespace chronon_c_api_detail
