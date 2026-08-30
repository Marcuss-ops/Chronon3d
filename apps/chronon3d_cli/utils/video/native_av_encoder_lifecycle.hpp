#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

#include <utility>

namespace chronon3d::cli::native_av {

/// Cleanup helpers for codec/frame resources. Container lifetime belongs to
/// media::MuxSession and is intentionally absent from this header.
#if 0
inline void close_io(AVFormatContext* value) noexcept {
    if (value && value->pb && value->oformat &&
        !(value->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&value->pb);
    }
}

inline void free_format(AVFormatContext*& value) noexcept {
    if (value) {
        close_io(value);
        avformat_free_context(value);
        value = nullptr;
    }
}

struct FormatContextDeleter {
    void operator()(AVFormatContext* value) const noexcept {
        if (value) {
            close_io(value);
            avformat_free_context(value);
        }
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* value) const noexcept {
        if (value) avcodec_free_context(&value);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* value) const noexcept {
        if (value) av_frame_free(&value);
    }
};

struct PacketDeleter {
    void operator()(AVPacket* value) const noexcept {
        if (value) av_packet_free(&value);
    }
};

struct BufferRefDeleter {
    void operator()(AVBufferRef* value) const noexcept {
        if (value) av_buffer_unref(&value);
    }
};

template <typename T, typename Deleter>
class unique_resource {
public:
    unique_resource() = default;
    explicit unique_resource(T* value) noexcept : value_(value) {}
    ~unique_resource() { reset(); }
    unique_resource(const unique_resource&) = delete;
    unique_resource& operator=(const unique_resource&) = delete;
    unique_resource(unique_resource&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    unique_resource& operator=(unique_resource&& other) noexcept {
        if (this != &other) { reset(); value_ = std::exchange(other.value_, nullptr); }
        return *this;
    }
    T* get() const noexcept { return value_; }
    T* release() noexcept { return std::exchange(value_, nullptr); }
    void reset(T* value = nullptr) noexcept { if (value_) Deleter{}(value_); value_ = value; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
private:
    T* value_{nullptr};
};

using unique_format_context = unique_resource<AVFormatContext, FormatContextDeleter>;
using unique_codec_context = unique_resource<AVCodecContext, CodecContextDeleter>;
using unique_frame = unique_resource<AVFrame, FrameDeleter>;
using unique_packet = unique_resource<AVPacket, PacketDeleter>;
using unique_buffer_ref = unique_resource<AVBufferRef, BufferRefDeleter>;
#endif

} // namespace chronon3d::cli::native_av
