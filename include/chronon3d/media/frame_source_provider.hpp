#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/media/decode_diagnostic.hpp>
#include <chronon3d/media/video/native_frame_importer.hpp>

#include <memory>
#include <optional>
#include <string>

namespace chronon3d {
struct RenderCounters;
}

namespace chronon3d::media {

class MediaFrameProvider {
public:
    virtual ~MediaFrameProvider() = default;

    virtual void set_counters(::chronon3d::RenderCounters*) {}
    virtual void set_native_frame_importer(
        std::shared_ptr<NativeFrameImporter> /*importer*/) {}
    virtual void set_gpu_hot_path_mode(GpuHotPathMode /*mode*/) {}

    /// Exact presentation-time decode contract. Source sample selection must
    /// be derived from this PTS coordinate; frame/fps compatibility adapters
    /// are intentionally not part of the provider API.
    virtual std::shared_ptr<Framebuffer> decode_frame_at(
        const std::string& path,
        RationalTime presentation_time,
        int width,
        int height) = 0;

    /// Diagnostic for the most recent failed decode on this provider. The
    /// framebuffer return remains source-compatible while callers gain a
    /// typed reason instead of interpreting every nullptr as the same failure.
    [[nodiscard]] virtual std::optional<DecodeDiagnostic>
    last_decode_diagnostic() const {
        return std::nullopt;
    }
};

} // namespace chronon3d::media