namespace {
enum AVPixelFormat select_cuda_format(AVCodecContext*, const enum AVPixelFormat* formats) {
    for (const enum AVPixelFormat* it = formats; *it != AV_PIX_FMT_NONE; ++it) {
        if (*it == AV_PIX_FMT_CUDA) return *it;
    }
    return formats[0];
}

std::shared_ptr<Framebuffer> frame_to_framebuffer(
    const AVFrame* frame, SwsContext*& sws, std::vector<uint8_t>& rgba,
    RenderCounters* counters, bool enable_swscale = true) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return nullptr;
    // Stress-harness bisection: when swscale is disabled, return a bare
    // framebuffer without exercising the SwsContext allocation/conversion
    // path. This isolates whether the sws cache/context lifetime is the
    // heap-corruption source independently of prefetch and frame cache.
    if (!enable_swscale) {
        return std::make_shared<Framebuffer>(frame->width, frame->height, false);
    }
    sws = sws_getCachedContext(sws, frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return nullptr;
    rgba.resize(static_cast<size_t>(frame->width) * frame->height * 4);
    uint8_t* dst_data[4] = {rgba.data(), nullptr, nullptr, nullptr};
    int dst_stride[4] = {frame->width * 4, 0, 0, 0};
    const auto start = profiling::now();
    const int ret = sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                              dst_data, dst_stride);
    const auto sws_dur = static_cast<uint64_t>(profiling::duration_ms(start, profiling::now()));
    if (counters) {
        counters->video_decode_sws_wall_ms.fetch_add(sws_dur, std::memory_order_relaxed);
        counters->swscale_ms.fetch_add(sws_dur, std::memory_order_relaxed);
    }
    if (ret != frame->height) return nullptr;
    profiling::FramebufferAllocationScope allocation_scope(
        profiling::FramebufferAllocationCategory::Video);
    auto fb = std::make_shared<Framebuffer>(frame->width, frame->height, false);
    const i32 stride = fb->allocated_width();
    Color* pixels = fb->data();
    const auto conv_start = profiling::now();
    const auto convert_rows = [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const uint8_t* row = rgba.data() + static_cast<size_t>(y) * frame->width * 4;
            Color* dst = pixels + static_cast<usize>(y) * stride;
            for (int x = 0; x < frame->width; ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4;
                dst[x] = Color{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f};
            }
        }
    };
    // Do not initialize/enter the process-wide TBB scheduler for tiny video
    // frames. Apart from being slower, doing so from multiple decoder
    // sessions makes startup unnecessarily contend on TBB's global runtime.
    constexpr std::size_t kParallelPixelThreshold = 4096;
    const auto pixels_count = static_cast<std::size_t>(frame->width) *
                              static_cast<std::size_t>(frame->height);
    if (pixels_count < kParallelPixelThreshold) {
        convert_rows(0, frame->height);
    } else {
        const int grain = std::max(16, frame->height / 16);
        parallel_for_tracked(tbb::blocked_range<int>(0, frame->height, grain),
            [&](const tbb::blocked_range<int>& rows) {
                convert_rows(rows.begin(), rows.end());
            });
    }
    const auto conv_dur = static_cast<uint64_t>(profiling::duration_ms(conv_start, profiling::now()));
    if (counters) {
        counters->cpu_pixel_conversion_ms.fetch_add(conv_dur, std::memory_order_relaxed);
        counters->software_color_convert_frames.fetch_add(1, std::memory_order_relaxed);
    }
    return fb;
}

} // namespace
