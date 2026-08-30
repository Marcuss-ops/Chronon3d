#pragma once

#include <chronon3d/runtime/device_scheduler.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>

#include <memory>
#include <optional>

namespace chronon3d::media {

/// Host-selected placement and persistent per-device video runtime.
/// Export setup consumes the reservation; it never performs a second reserve.
struct VideoJobExecutionContext {
    std::optional<runtime::DeviceReservation> reservation;
    std::shared_ptr<VideoRuntimeRegistry> video_runtimes;
    runtime::DeviceId device_id{0};
    std::uint32_t physical_device_index{0};
    std::int32_t cuda_device_ordinal{-1};
};

} // namespace chronon3d::media
