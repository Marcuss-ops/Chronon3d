#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/runtime/bounded_channel.hpp>
#include "../../../utils/video/direct_yuv_frame.hpp"
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/render_surface_handle.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <condition_variable>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <variant>

namespace chronon3d::cli {

/// Bounded ownership ring for GPU encode surfaces. A slot is acquired by the
/// render thread before a frame package is published and released by the
/// writer after the encoder has consumed that surface.
class FrameInteropRing {
public:
    // Six slots keep decode, composition, and NVENC ownership overlapped
    // without allowing an unbounded surface queue to grow.
    static constexpr std::size_t kSlotCount = 6;
    static constexpr std::size_t kInvalidSlot = kSlotCount;

    explicit FrameInteropRing(std::size_t slots = kSlotCount)
        : slot_count_(std::min(slots, kSlotCount)) {}

    FrameInteropRing(const FrameInteropRing&) = delete;
    FrameInteropRing& operator=(const FrameInteropRing&) = delete;

    [[nodiscard]] std::size_t acquire(const CancellationToken* token = nullptr) {
        std::unique_lock lock(mutex_);
        const auto wait_start = std::chrono::steady_clock::now();
        bool waited = false;
        for (;;) {
            if (closed_ || (token && token->is_cancelled())) return kInvalidSlot;
            for (std::size_t i = 0; i < slot_count_; ++i) {
                const auto slot = (next_slot_ + i) % slot_count_;
                if (!busy_[slot]) {
                    busy_[slot] = true;
                    next_slot_ = (slot + 1) % slot_count_;
                    if (waited) {
                        wait_count_.fetch_add(1, std::memory_order_relaxed);
                        wait_us_.fetch_add(static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - wait_start).count()),
                            std::memory_order_relaxed);
                    }
                    return slot;
                }
            }
            waited = true;
            // Poll cancellation while the writer is still consuming the
            // bounded ring. A plain wait() could strand the render thread if
            // cancellation happens without a slot release notification.
            condition_.wait_for(lock, std::chrono::milliseconds(1));
        }
    }

    void release(std::size_t slot) noexcept {
        if (slot >= slot_count_) return;
        {
            std::lock_guard lock(mutex_);
            busy_[slot] = false;
        }
        condition_.notify_one();
    }

    void close() noexcept {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

    /// Number of slots currently owned by the producer/writer (GPU encode
    /// surfaces in flight).  O(slot_count) — used only for Perfetto counter
    /// tracks (frames_in_flight), guarded by tracing::TracingActive().
    [[nodiscard]] std::size_t busy_count() const noexcept {
        std::lock_guard lock(mutex_);
        std::size_t count = 0;
        for (std::size_t i = 0; i < slot_count_; ++i) {
            if (busy_[i]) ++count;
        }
        return count;
    }

    [[nodiscard]] std::uint64_t wait_count() const noexcept {
        return wait_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t wait_us() const noexcept {
        return wait_us_.load(std::memory_order_relaxed);
    }

private:
    const std::size_t slot_count_;
    std::array<bool, kSlotCount> busy_{};
    std::size_t next_slot_{0};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool closed_{false};
    std::atomic<std::uint64_t> wait_count_{0};
    std::atomic<std::uint64_t> wait_us_{0};
};

// ── Explicit frame variants ────────────────────────────────────────────────
// Direct-YUV must not carry FullGraph ownership/lifetime state. Keeping the
// two payloads as a variant makes accidental use of Vulkan surfaces or the
// CPU arena in the direct path a type error instead of a convention.

struct FullGraphFramePackage {
    Frame frame_number{0};
    std::shared_ptr<Framebuffer> framebuffer;
    std::shared_ptr<FramebufferArena> arena;
    graph::RenderBackend* backend{nullptr};
    runtime::RenderSurfaceRegistry* surface_registry{nullptr};
    runtime::RenderSurfaceHandle source_surface{runtime::kInvalidRenderSurfaceHandle};
    runtime::RenderSurfaceHandle native_surface{runtime::kInvalidRenderSurfaceHandle};
    std::size_t interop_slot{FrameInteropRing::kInvalidSlot};
    bool native_surface_ready{false};
};

struct DirectYuvFramePackage {
    Frame frame_number{0};
    std::shared_ptr<DirectYuvFrame> direct_yuv;
};

using RenderFramePackage = std::variant<FullGraphFramePackage, DirectYuvFramePackage>;

} // namespace chronon3d::cli
