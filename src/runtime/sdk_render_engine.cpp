// ============================================================================
// src/runtime/sdk_render_engine.cpp
//
// Fase A3 — sdk::RenderEngine PIMPL implementation.
//
// This is the SINGLE bridge between the public SDK surface and the
// internal adapter (`chronon3d::RenderEngine`).  No other translation
// layer should exist.
//
// For the full architecture diagram, see the canonical header:
//   <chronon3d/api/render_engine.hpp>  (§ INTERNAL ADAPTER for sdk::RenderEngine)
//
// render() converts the internal shared_ptr<Framebuffer> (float RGBA
// pixels) to an RGBA8 pixel buffer owned by this Impl.  The buffer
// remains valid until the next render() call or engine destruction,
// per the public API contract.
// ============================================================================

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/api/render_engine.hpp>       // chronon3d::RenderEngine (legacy impl)
#include <chronon3d/core/memory/framebuffer.hpp>  // Framebuffer, Color
#include <chronon3d/math/color.hpp>               // Color

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace chronon3d::sdk {

// ── Convert sdk::RenderSettings → chronon3d::RenderSettings ──────────────
// The SDK settings are a minimal neutral POD; we map them to the internal
// settings type with safe defaults for backend-specific knobs.
namespace {

chronon3d::RenderSettings convert_settings(const RenderSettings& sdk) {
    chronon3d::RenderSettings internal;
    internal.ssaa_factor      = static_cast<float>(std::max(1, sdk.antialiasing_samples));
    internal.motion_blur.mode = sdk.motion_blur
        ? chronon3d::MotionBlurMode::TemporalAccumulation
        : chronon3d::MotionBlurMode::Off;
    internal.motion_blur.samples          = sdk.motion_blur ? 8 : 1;
    internal.motion_blur.shutter_angle_deg = 180.0f;
    internal.motion_blur.shutter_phase_deg = 0.0f;
    internal.dirty.enabled                = sdk.dirty_rects;
    internal.force_scalar_normal_blend    = sdk.deterministic;
    // TODO(P1): wire sdk.max_threads through to the execution scheduler.
    // Currently auto-detected; the SDK knob is reserved but not yet mapped.
    (void)sdk.max_threads;
    return internal;
}

// ── Convert float RGBA framebuffer to uint8 RGBA pixel buffer ─────────
// Framebuffer stores Color pixels as {float r, g, b, a} in [0, 1].
// RenderOutput expects tightly-packed RGBA8 bytes.
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
        dst[i * 4 + 0] = static_cast<std::uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 1] = static_cast<std::uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 2] = static_cast<std::uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 3] = static_cast<std::uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f));
    }
}

} // namespace

// ── sdk::RenderEngine::Impl ───────────────────────────────────────────────

struct RenderEngine::Impl {
    chronon3d::RenderEngine       engine;         // legacy OPP-side engine
    std::vector<std::uint8_t>     pixel_buffer;   // RGBA8 conversion buffer
    RenderOutput                  last_output{};   // cached for pointer validity

    Impl() : engine() {}

    explicit Impl(RenderSettings settings)
        : engine() {
        engine.set_settings(convert_settings(settings));
    }
};

// ── Construction / destruction / move ─────────────────────────────────────

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

// ── render() ──────────────────────────────────────────────────────────────

chronon3d::Result<RenderOutput, RenderError>
RenderEngine::render(const chronon3d::Composition& composition, Frame req) {
    try {
        auto fb = m_impl->engine.render(composition, chronon3d::Frame{req.value});
        if (!fb) {
            RenderError err;
            err.code    = RenderErrorCode::RuntimeFailure;
            err.message = "legacy RenderEngine::render() returned null framebuffer";
            return err;
        }

        // Convert float RGBA pixels to RGBA8 for the public API.
        framebuffer_to_rgba8(*fb, m_impl->pixel_buffer);

        RenderOutput out;
        out.frame              = req;
        out.width              = fb->width();
        out.height             = fb->height();
        out.format             = PixelFormat::Rgba8;
        out.bytes_per_row      = static_cast<std::size_t>(fb->width()) * 4;
        out.pixels             = m_impl->pixel_buffer.data();

        m_impl->last_output = out;

        return out;
    } catch (const std::exception& e) {
        RenderError err;
        err.code    = RenderErrorCode::RuntimeFailure;
        err.message = std::string("render exception: ") + e.what();
        return err;
    } catch (...) {
        RenderError err;
        err.code    = RenderErrorCode::InternalError;
        err.message = "render: unknown exception";
        return err;
    }
}

// ── set_settings() ────────────────────────────────────────────────────────

void RenderEngine::set_settings(const RenderSettings& settings) {
    m_impl->engine.set_settings(convert_settings(settings));
}

// ── set_assets_root() ─────────────────────────────────────────────────────

void RenderEngine::set_assets_root(std::filesystem::path root) {
    // Canonicalize relative paths to absolute: the legacy engine's
    // AssetResolver::mount() requires an absolute path.
    if (root.is_relative()) {
        root = std::filesystem::absolute(root);
    }
    m_impl->engine.set_assets_root(std::move(root));
}

} // namespace chronon3d::sdk
