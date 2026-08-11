#pragma once

// ---------------------------------------------------------------------------
// encoder_frame_pool.hpp — Reusable encoder-destination storage.
//
// The pool owns the buffers used by RGBA→encoder-format conversion. A
// BorrowedEncoderFrame keeps its slot checked out until destruction, so a
// synchronous sink can consume the converted bytes without an intermediate
// Chronon staging copy. The FFmpeg process boundary still copies bytes into
// the child process pipe; this pool only removes copies inside Chronon.
//
// Thread-safety: the pool is intentionally not thread-safe. Create one pool
// per serial encoder/writer owner, matching FrameConversionService's
// threading contract.
// ---------------------------------------------------------------------------

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/video/video_frame.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace chronon3d::video {

class EncoderFramePool;

/// A move-only RAII borrow of one encoder frame slot.
struct BorrowedEncoderFrame {
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};
    int width{0};
    int height{0};
    std::span<uint8_t> storage{};
    FramePlanes planes{};

    BorrowedEncoderFrame() = default;
    ~BorrowedEncoderFrame();

    BorrowedEncoderFrame(const BorrowedEncoderFrame&) = delete;
    BorrowedEncoderFrame& operator=(const BorrowedEncoderFrame&) = delete;
    BorrowedEncoderFrame(BorrowedEncoderFrame&& other) noexcept;
    BorrowedEncoderFrame& operator=(BorrowedEncoderFrame&& other) noexcept;

    /// False when the pool had no available slot or was invalidly configured.
    explicit operator bool() const noexcept { return !storage.empty(); }

private:
    struct State;
    std::shared_ptr<State> state_;
    std::size_t slot_index_{static_cast<std::size_t>(-1)};

    BorrowedEncoderFrame(std::shared_ptr<State> state, std::size_t slot_index,
                         EncoderPixelFormat frame_format, int frame_width,
                         int frame_height, std::span<uint8_t> bytes,
                         FramePlanes frame_planes) noexcept;
    friend class EncoderFramePool;
};

/// Fixed-capacity pool for tightly packed encoder frame destinations.
class EncoderFramePool {
public:
    struct Config {
        int width{0};
        int height{0};
        EncoderPixelFormat format{EncoderPixelFormat::YUV420P};
        std::size_t slot_count{4};
    };

    struct Stats {
        std::size_t slots_allocated{0};
        std::size_t slot_reuses{0};
        std::size_t acquisitions{0};
        std::size_t active_slots{0};
        std::size_t peak_active_slots{0};
        std::size_t allocated_bytes{0};
    };

    EncoderFramePool() noexcept = default;
    explicit EncoderFramePool(Config config);
    ~EncoderFramePool() = default;

    EncoderFramePool(const EncoderFramePool&) = delete;
    EncoderFramePool& operator=(const EncoderFramePool&) = delete;
    EncoderFramePool(EncoderFramePool&&) noexcept = default;
    EncoderFramePool& operator=(EncoderFramePool&&) noexcept = default;

    /// Borrow an available slot. The returned frame releases it on destruction.
    /// A default/empty frame means all slots are currently borrowed or the
    /// configuration was invalid.
    [[nodiscard]] BorrowedEncoderFrame acquire() noexcept;

    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] std::size_t frame_bytes() const noexcept { return frame_bytes_; }
    [[nodiscard]] Stats stats() const noexcept;

    /// Tight packed byte count for an encoder format, or zero when invalid.
    [[nodiscard]] static std::size_t encoded_size(
        int width, int height, EncoderPixelFormat format) noexcept;

private:
    std::shared_ptr<BorrowedEncoderFrame::State> state_;
    Config config_{};
    std::size_t frame_bytes_{0};
};

} // namespace chronon3d::video
