#pragma once

#include <chronon3d/media/frame_source_provider.hpp>

namespace chronon3d::video {

/// Concrete video decoder interface — inherits the abstract
/// media::MediaFrameProvider contract and adds no extra methods.
/// The render graph now uses media::MediaFrameProvider* directly;
/// this alias-interface remains for backward compatibility and
/// to keep concrete implementations namespaced under video::
class VideoFrameDecoder : public media::MediaFrameProvider {
    // decode_frame() is inherited from MediaFrameProvider.
    // Concrete implementations override it as before.
};

} // namespace chronon3d::video
