#pragma once

#include "ffmpeg_pipe_encoder.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <fstream>
#include <vector>

namespace chronon3d::cli {

// ── NullRenderEncoder ──────────────────────────────────────────────────────
// Renders frames but does NOT convert or write video.
// Use to isolate pure render graph cost.
class NullRenderEncoder final : public IVideoEncoder {
public:
    bool open(const FfmpegPipeOptions& options) override {
        options_ = options;
        frames_written_ = 0;
        return true;
    }

    bool write_frame(const Framebuffer& /*fb*/) override {
        ++frames_written_;
        return true;
    }

    bool write_frame_async(const Framebuffer&, std::shared_ptr<Framebuffer> owner) override {
        owner.reset();
        ++frames_written_;
        return true;
    }

    bool close() override {
        return true;
    }

    [[nodiscard]] uint64_t frames_written() const override {
        return frames_written_;
    }

private:
    FfmpegPipeOptions options_{};
    uint64_t frames_written_{0};
};

// ── NullConvertEncoder ─────────────────────────────────────────────────────
// Renders + converts frames to the selected pixel format, but does NOT write
// to any pipe or file.  Use to isolate render + conversion cost.
class NullConvertEncoder final : public IVideoEncoder {
public:
    bool open(const FfmpegPipeOptions& options) override;
    bool write_frame(const Framebuffer& fb) override;
    bool write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) override {
        const bool ok = write_frame(fb);
        owner.reset();
        return ok;
    }
    bool close() override { return true; }

    void set_counters(chronon3d::RenderCounters* counters) override {
        counters_ = counters;
    }

    [[nodiscard]] uint64_t frames_written() const override {
        return frames_written_;
    }

private:
    FfmpegPipeOptions options_{};
    std::vector<uint8_t> buffer_;
    uint64_t frames_written_{0};
    chronon3d::RenderCounters* counters_{nullptr};

    bool convert_selected_format(const Framebuffer& fb, uint8_t* dst);
};

// ── RawVideoSinkEncoder ────────────────────────────────────────────────────
// Renders + converts + writes raw pixel bytes to a file (no FFmpeg/container).
// Use to isolate render + convert + write cost without FFmpeg overhead.
class RawVideoSinkEncoder final : public IVideoEncoder {
public:
    bool open(const FfmpegPipeOptions& options) override;
    bool write_frame(const Framebuffer& fb) override;
    bool write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) override {
        const bool ok = write_frame(fb);
        owner.reset();
        return ok;
    }
    bool close() override;

    void set_counters(chronon3d::RenderCounters* counters) override {
        counters_ = counters;
    }

    [[nodiscard]] uint64_t frames_written() const override {
        return frames_written_;
    }

private:
    FfmpegPipeOptions options_{};
    std::ofstream out_;
    std::vector<uint8_t> buffer_;
    uint64_t frames_written_{0};
    chronon3d::RenderCounters* counters_{nullptr};

    bool convert_selected_format(const Framebuffer& fb, uint8_t* dst);
};

} // namespace chronon3d::cli
