#pragma once

struct AVFrame;

namespace chronon3d::media {

/// RAII ownership wrapper around a reference-counted FFmpeg AVFrame.
///
/// Replaces `std::shared_ptr<AVFrame>` in the public Chronon media API:
/// FFmpeg already refcounts the underlying buffers, so a copy of this
/// wrapper shares them (av_frame_ref / av_frame_clone) instead of
/// deep-copying pixel data, a move steals the pointer, and the destructor
/// releases it (av_frame_free).
class HwFrameRef {
public:
    HwFrameRef() = default;

    /// Takes ownership of `frame` (must have been allocated with
    /// av_frame_alloc or av_frame_clone).
    explicit HwFrameRef(AVFrame* frame) noexcept;

    HwFrameRef(const HwFrameRef& other);
    HwFrameRef& operator=(const HwFrameRef& other);

    HwFrameRef(HwFrameRef&& other) noexcept;
    HwFrameRef& operator=(HwFrameRef&& other) noexcept;

    ~HwFrameRef();

    [[nodiscard]] AVFrame* get() const noexcept { return frame_; }
    explicit operator bool() const noexcept { return frame_ != nullptr; }
    AVFrame* operator->() const noexcept { return frame_; }

    /// Release the owned reference (if any).
    void reset() noexcept;

private:
    void release() noexcept;

    AVFrame* frame_{nullptr};
};

} // namespace chronon3d::media