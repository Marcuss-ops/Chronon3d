#pragma once

#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/media/decode_result.hpp>
#include <chronon3d/media/video/native_frame_importer.hpp>

#include <memory>
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

    /// Exact presentation-time decode contract. Source sample selection is
    /// derived only from this rational PTS coordinate. Success, EOF and
    /// failure are distinct typed outcomes.
    virtual DecodeResult decode_frame_at(
        const std::string& path,
        RationalTime presentation_time,
        int width,
        int height) = 0;
};

} // namespace chronon3d::media
