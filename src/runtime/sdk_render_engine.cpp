// ============================================================================
// src/runtime/sdk_render_engine.cpp
//
// SINGLE bridge between the public sdk::RenderEngine surface and the internal
// chronon3d::RenderEngine adapter. The public boundary owns RGBA8 conversion
// and translates every internal failure into RenderError.
// ============================================================================

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace chronon3d::sdk {

namespace {

chronon3d::RenderSettings convert_settings(const RenderSettings& sdk) {
    chronon3d::RenderSettings internal;
    internal.ssaa_factor      = static_cast<float>(std::max(1, sdk.antialiasing_samples));
    internal.motion_blur.mode = sdk.motion_blur
        ? chronon3d::MotionBlurMode::TemporalAccumulation
        : chronon3d::MotionBlurMode::Off;
    internal.motion_blur.samples           = sdk.motion_blur ? 8 : 1;
    internal.motion_blur.shutter_angle_deg = 180.0f;
    internal.motion_blur.shutter_phase_deg = 0.0f;
    internal.dirty.enabled                 = sdk.dirty_rects;
    internal.force_scalar_normal_blend     = sdk.deterministic;
    // TODO(P1): wire sdk.max_threads through to the execution scheduler.
    (void)sdk.max_threads;
    return internal;
}

void framebuffer_to_rgba8(const Framebuffer& fb,
                          std::vector<std::uint8_t>& out) {
    const i32 w = fb.width();
    const i32 h = fb.height();
    const usize count = static_cast<usize>(w) * h * 4;
    out.resize(count);

    const Color* src = fb.data();
    std::uint8_t* dst = out.data();

    for (usize i = 0; i < static_cast<usize>(w) * h; ++i) {
        const Color& c = src[i];
        dst[i * 4 + 0] = static_cast<std::uint8_t>(
            std::clamp(c.r * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 1] = static_cast<std::uint8_t>(
            std::clamp(c.g * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 2] = static_cast<std::uint8_t>(
            std::clamp(c.b * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 3] = static_cast<std::uint8_t>(
            std::clamp(c.a * 255.0f, 0.0f, 255.0f));
    }
}

RenderError runtime_error(std::string message) {
    return RenderError{
        .code = RenderErrorCode::RuntimeFailure,
        .message = std::move(message),
    };
}

} // namespace

struct RenderEngine::Impl {
    chronon3d::RenderEngine   engine;
    std::vector<std::uint8_t> pixel_buffer;
    RenderOutput              last_output{};

    Impl() : engine() {}

    explicit Impl(RenderSettings settings)
        : engine() {
        engine.set_settings(convert_settings(settings));
    }
};

RenderEngine::RenderEngine()
    : m_impl(std::make_unique<Impl>())
{}

RenderEngine::RenderEngine(RenderSettings settings)
    : m_impl(std::make_unique<Impl>(settings))
{}

RenderEngine::~RenderEngine() = default;

RenderEngine::RenderEngine(RenderEngine&& other) noexcept
    : m_impl(std::move(other.m_impl))
{}

RenderEngine& RenderEngine::operator=(RenderEngine&& other) noexcept {
    if (this != &other) m_impl = std::move(other.m_impl);
    return *this;
}

chronon3d::Result<RenderOutput, RenderError>
RenderEngine::render(const chronon3d::Composition& composition, Frame frame) {
    try {
        auto framebuffer = m_impl->engine.render(
            composition, chronon3d::Frame{frame.integral()});

        // The internal graph may retain a transparent/partial framebuffer for
        // diagnostics after a node failure. Public SDK callers must never
        // mistake that buffer for a successful render.
        if (const auto error = m_impl->engine.last_render_error()) {
            const std::string message = error->message.empty()
                ? "internal render graph reported a frame error"
                : error->message;
            return runtime_error(message);
        }

        if (!framebuffer) {
            return runtime_error(
                "internal RenderEngine::render() returned a null framebuffer");
        }

        framebuffer_to_rgba8(*framebuffer, m_impl->pixel_buffer);

        RenderOutput output;
        output.frame         = frame;
        output.width         = framebuffer->width();
        output.height        = framebuffer->height();
        output.format        = PixelFormat::Rgba8;
        output.bytes_per_row = static_cast<std::size_t>(framebuffer->width()) * 4;
        output.pixels        = m_impl->pixel_buffer.data();

        m_impl->last_output = output;
        return output;
    } catch (const std::exception& error) {
        return runtime_error(std::string("render exception: ") + error.what());
    } catch (...) {
        return RenderError{
            .code = RenderErrorCode::InternalError,
            .message = "render: unknown exception",
        };
    }
}

void RenderEngine::set_settings(const RenderSettings& settings) {
    m_impl->engine.set_settings(convert_settings(settings));
}

void RenderEngine::set_assets_root(std::filesystem::path root) {
    // A relative value is an explicit caller choice, not a resolver fallback.
    // Canonicalize it once at assignment so later CWD changes cannot alter the
    // engine-local mount.
    if (root.is_relative()) {
        root = std::filesystem::absolute(root);
    }
    m_impl->engine.set_assets_root(std::move(root));
}

} // namespace chronon3d::sdk
