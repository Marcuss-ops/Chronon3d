#pragma once

// ---------------------------------------------------------------------------
// encoder_backend.hpp — Registry and interface for hardware encoder backends.
//
// Priority 6 scaffolding — interfaces only, no implementation yet.
//
// Rationale: define the abstraction boundary now so that the software FFmpeg
// path (Priority 5) is written against an interface, not a concrete type.
// NVENC / VAAPI / AMF implementations will register here in a future session.
//
// Rule: if no hardware backend is available or selected, the registry falls
// back to SoftwareFFmpeg transparently.  The caller never gets a null backend.
// ---------------------------------------------------------------------------

#include <chronon3d/video/frame_converter.hpp>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::video {

/// Identifies the physical encoder implementation.
enum class EncodeBackend {
    SoftwareFFmpeg,  ///< libx264 / libx265 via popen pipe — always available
    NVENC,           ///< NVIDIA NVENC via FFmpeg hwaccel
    VAAPI,           ///< Linux VAAPI (Intel Quick Sync, AMD VCE)
    AMF,             ///< AMD Advanced Media Framework
};

/// What a given backend can do.
struct BackendCapabilities {
    bool supports_h264{false};
    bool supports_h265{false};
    bool supports_nv12{false};
    bool supports_yuv420p{false};
    bool supports_zero_copy{false};  ///< Can consume Framebuffer GPU memory directly
};

/// Abstract interface every backend must implement.
class EncoderBackend {
public:
    virtual ~EncoderBackend() = default;

    [[nodiscard]] virtual EncodeBackend    backend_kind()  const = 0;
    [[nodiscard]] virtual std::string      name()          const = 0;
    [[nodiscard]] virtual BackendCapabilities capabilities() const = 0;

    /// Returns false if the hardware is absent or the driver is not installed.
    [[nodiscard]] virtual bool is_available() const = 0;
};

/// Singleton registry that selects the best available backend for a request.
///
/// Usage (Priority 6):
///   EncoderBackendRegistry::instance().register_backend(
///       std::make_unique<NvencBackend>());
///   auto* backend = EncoderBackendRegistry::instance()
///       .select_best({.supports_h264 = true, .supports_nv12 = true});
class EncoderBackendRegistry {
public:
    static EncoderBackendRegistry& instance();

    /// Register a backend.  Backends are probed in registration order.
    void register_backend(std::unique_ptr<EncoderBackend> backend);

    /// Returns the first available backend that satisfies `required`.
    /// Falls back to SoftwareFFmpeg if nothing else matches.
    /// Never returns nullptr (throws if software fallback also missing).
    [[nodiscard]] EncoderBackend* select_best(
        const BackendCapabilities& required) const;

    [[nodiscard]] const std::vector<std::unique_ptr<EncoderBackend>>& backends() const {
        return backends_;
    }

private:
    EncoderBackendRegistry() = default;
    std::vector<std::unique_ptr<EncoderBackend>> backends_;
};

} // namespace chronon3d::video
