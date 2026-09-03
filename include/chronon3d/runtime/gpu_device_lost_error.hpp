#pragma once

#include <stdexcept>

namespace chronon3d::runtime {

/// Fatal GPU failure boundary. A backend throws this only after the device has
/// reported a terminal device-loss condition; callers must not retry work on
/// the same worker/device instance.
class GpuDeviceLostError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace chronon3d::runtime
