#include <chronon3d/media/frame_conversion/encoder_frame_pool.hpp>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace chronon3d::video {

namespace {
constexpr std::size_t kNoSlot = std::numeric_limits<std::size_t>::max();
}

struct BorrowedEncoderFrame::State {
    struct Slot {
        std::vector<uint8_t> bytes;
        bool borrowed{false};
    };

    std::vector<Slot> slots;
    std::size_t frame_bytes{0};
    std::size_t acquisitions{0};
    std::size_t slot_reuses{0};
    std::size_t active_slots{0};
    std::size_t peak_active_slots{0};
};

std::size_t EncoderFramePool::encoded_size(
    int width, int height, EncoderPixelFormat format) noexcept
{
    if (width <= 0 || height <= 0
        || width > chronon3d::media::video::kMaxFrameDimension
        || height > chronon3d::media::video::kMaxFrameDimension) {
        return 0;
    }
    const auto pixels = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (pixels > chronon3d::media::video::kMaxPixelCount) return 0;
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h) return 0;
    const auto y = w * h;

    switch (format) {
        case EncoderPixelFormat::YUV420P:
        case EncoderPixelFormat::NV12:
            if ((width & 1) != 0 || (height & 1) != 0) return 0;
            return y + y / 2;
        case EncoderPixelFormat::RGB24:
            return y > std::numeric_limits<std::size_t>::max() / 3
                ? 0 : y * 3;
        case EncoderPixelFormat::RGBA8:
            return y > std::numeric_limits<std::size_t>::max() / 4
                ? 0 : y * 4;
    }
    return 0;
}

EncoderFramePool::EncoderFramePool(Config config)
    : config_(config)
    , frame_bytes_(encoded_size(config.width, config.height, config.format))
{
    if (frame_bytes_ == 0 || config_.slot_count == 0) return;

    auto state = std::make_shared<BorrowedEncoderFrame::State>();
    state->frame_bytes = frame_bytes_;
    state->slots.resize(config_.slot_count);
    for (auto& slot : state->slots) {
        slot.bytes.resize(frame_bytes_);
    }
    state_ = std::move(state);
}

BorrowedEncoderFrame EncoderFramePool::acquire() noexcept {
    if (!state_) return {};

    for (std::size_t i = 0; i < state_->slots.size(); ++i) {
        auto& slot = state_->slots[i];
        if (slot.borrowed) continue;

        slot.borrowed = true;
        ++state_->acquisitions;
        ++state_->active_slots;
        state_->peak_active_slots = std::max(
            state_->peak_active_slots, state_->active_slots);
        if (state_->acquisitions > state_->slots.size()) {
            ++state_->slot_reuses;
        }

        auto* bytes = slot.bytes.data();
        FramePlanes planes{};
        const auto y_size = static_cast<std::size_t>(config_.width) * config_.height;
        switch (config_.format) {
            case EncoderPixelFormat::YUV420P:
                planes.y = bytes;
                planes.u = bytes + y_size;
                planes.v = planes.u + y_size / 4;
                planes.stride_y = config_.width;
                planes.stride_u = config_.width / 2;
                planes.stride_v = config_.width / 2;
                break;
            case EncoderPixelFormat::NV12:
                planes.y = bytes;
                planes.uv = bytes + y_size;
                planes.stride_y = config_.width;
                planes.stride_uv = config_.width;
                break;
            case EncoderPixelFormat::RGB24:
            case EncoderPixelFormat::RGBA8:
                planes.y = bytes;
                break;
        }
        return BorrowedEncoderFrame{state_, i, config_.format, config_.width,
                                    config_.height,
                                    std::span<uint8_t>(bytes, frame_bytes_), planes};
    }
    return {};
}

EncoderFramePool::Stats EncoderFramePool::stats() const noexcept {
    if (!state_) return {};
    return Stats{
        .slots_allocated = state_->slots.size(),
        .slot_reuses = state_->slot_reuses,
        .acquisitions = state_->acquisitions,
        .active_slots = state_->active_slots,
        .peak_active_slots = state_->peak_active_slots,
        .allocated_bytes = state_->slots.size() * state_->frame_bytes,
    };
}

BorrowedEncoderFrame::BorrowedEncoderFrame(
    std::shared_ptr<State> state, std::size_t slot_index,
    EncoderPixelFormat frame_format, int frame_width, int frame_height,
    std::span<uint8_t> bytes, FramePlanes frame_planes) noexcept
    : format(frame_format)
    , width(frame_width)
    , height(frame_height)
    , storage(bytes)
    , planes(frame_planes)
    , state_(std::move(state))
    , slot_index_(slot_index)
{
}

BorrowedEncoderFrame::~BorrowedEncoderFrame() {
    if (state_ && slot_index_ != kNoSlot && slot_index_ < state_->slots.size()) {
        auto& slot = state_->slots[slot_index_];
        if (slot.borrowed) {
            slot.borrowed = false;
            if (state_->active_slots > 0) --state_->active_slots;
        }
    }
}

BorrowedEncoderFrame::BorrowedEncoderFrame(BorrowedEncoderFrame&& other) noexcept
    : format(other.format)
    , width(other.width)
    , height(other.height)
    , storage(other.storage)
    , planes(other.planes)
    , state_(std::move(other.state_))
    , slot_index_(other.slot_index_)
{
    other.storage = {};
    other.planes = {};
    other.slot_index_ = kNoSlot;
}

BorrowedEncoderFrame& BorrowedEncoderFrame::operator=(BorrowedEncoderFrame&& other) noexcept {
    if (this == &other) return *this;
    if (state_ && slot_index_ != kNoSlot && slot_index_ < state_->slots.size()) {
        auto& slot = state_->slots[slot_index_];
        if (slot.borrowed) {
            slot.borrowed = false;
            if (state_->active_slots > 0) --state_->active_slots;
        }
    }
    format = other.format;
    width = other.width;
    height = other.height;
    storage = other.storage;
    planes = other.planes;
    state_ = std::move(other.state_);
    slot_index_ = other.slot_index_;
    other.storage = {};
    other.planes = {};
    other.slot_index_ = kNoSlot;
    return *this;
}

} // namespace chronon3d::video
