#pragma once

// Shared helpers for the installed-SDK consumers.
//
// The only production execution input is compiler::CompiledProgram. Consumers
// therefore author a RenderPlan JSON document and use compile_plan_json() +
// render_program(); no Composition executor or SceneBuilder render path exists.

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_output.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>

namespace consumer {

inline std::shared_ptr<const chronon3d::compiler::CompiledProgram>
compile_plan(chronon3d::sdk::RenderEngine& engine, std::string_view json,
             const char* label) {
    auto program = engine.compile_plan_json(json);
    if (!program) {
        std::fprintf(stderr,
                     "[consumer-error] %s: compile_plan_json failed: %s\n",
                     label, program.error().message.c_str());
        return {};
    }
    return program.value();
}

/// Copy the SDK's canonical RGBA8 output into a public Framebuffer so the
/// consumer can reuse save_png() and the pixel probes.
inline void copy_pixels(const chronon3d::sdk::RenderOutput& output,
                        chronon3d::Framebuffer& framebuffer) {
    const std::uint8_t* src = output.pixels;
    const std::size_t row_stride = output.bytes_per_row > 0
        ? static_cast<std::size_t>(output.bytes_per_row)
        : static_cast<std::size_t>(output.width) * 4u;
    constexpr float inv = 1.0f / 255.0f;

    for (std::int32_t y = 0; y < output.height; ++y) {
        for (std::int32_t x = 0; x < output.width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * row_stride
                + static_cast<std::size_t>(x) * 4u;
            framebuffer.set_pixel(x, y,
                chronon3d::Color{src[idx + 0] * inv, src[idx + 1] * inv,
                                 src[idx + 2] * inv, 1.0f});
        }
    }
}

} // namespace consumer
