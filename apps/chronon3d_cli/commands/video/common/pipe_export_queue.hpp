#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/runtime/bounded_channel.hpp>
#include <chronon3d/media/video/direct_yuv_frame.hpp>
#include <chronon3d/core/types/frame.hpp>
#include "gpu_slot_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>

namespace chronon3d::cli {

struct FullGraphFramePackage {
    Frame frame_number{0};
    runtime::GpuSlotPool::Lease slot;
    std::shared_ptr<Framebuffer> cpu_fallback;
    std::shared_ptr<FramebufferArena> cpu_arena;
};

struct DirectYuvFramePackage {
    Frame frame_number{0};
    std::shared_ptr<media::video::DirectYuvFrame> direct_yuv;
};

using RenderFramePackage = std::variant<FullGraphFramePackage, DirectYuvFramePackage>;

} // namespace chronon3d::cli
