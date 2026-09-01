namespace chronon3d::media {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
AVBufferRef* VideoDeviceRuntime::ref_cuda_frames(
    std::uint32_t width, std::uint32_t height, int sw_format,
    std::string& reason) {
    if (width == 0 || height == 0) {
        reason = "invalid CUDA frames dimensions";
        return nullptr;
    }
    if (!ensure_initialized(reason)) return nullptr;
    std::lock_guard lock(mutex_);
    const CudaFramesKey key{width, height, sw_format};
    if (const auto it = cuda_frames_.find(key); it != cuda_frames_.end()) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->cuda_frames_cache_hit
                .fetch_add(1, std::memory_order_relaxed);
        }
        return av_buffer_ref(it->second);
    }
    if (profiling::g_current_counters) {
        profiling::g_current_counters->cuda_frames_cache_miss
            .fetch_add(1, std::memory_order_relaxed);
    }
    if (!cuda_hwdevice_) {
        reason = "CUDA hwdevice is unavailable for frames context";
        return nullptr;
    }
    AVBufferRef* frames_ref = av_hwframe_ctx_alloc(cuda_hwdevice_);
    if (!frames_ref) {
        reason = "av_hwframe_ctx_alloc failed";
        return nullptr;
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(frames_ref->data);
    frames->format = AV_PIX_FMT_CUDA;
    frames->sw_format = static_cast<AVPixelFormat>(sw_format);
    frames->width = static_cast<int>(width);
    frames->height = static_cast<int>(height);
    frames->initial_pool_size = 8;
    if (av_hwframe_ctx_init(frames_ref) < 0) {
        reason = "av_hwframe_ctx_init failed";
        av_buffer_unref(&frames_ref);
        return nullptr;
    }
    cuda_frames_.emplace(key, frames_ref);
    return av_buffer_ref(frames_ref);
}
#endif

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
std::shared_ptr<const CudaImageResource> VideoDeviceRuntime::get_or_upload_image(
    const assets::ContentDigest& digest,
    const ImageDecodeOptions& options,
    std::uint32_t width,
    std::uint32_t height,
    std::span<const float> rgba,
    bool& cache_hit,
    double& upload_ms,
    std::string& reason) {
    cache_hit = false;
    upload_ms = 0.0;
    if (width == 0 || height == 0 || rgba.empty()) {
        reason = "invalid CUDA image dimensions or payload";
        return nullptr;
    }
    if (digest == assets::ContentDigest{}) {
        reason = "CUDA image cache requires a valid content digest";
        return nullptr;
    }
    if (!ensure_initialized(reason)) return nullptr;
    if (cuCtxSetCurrent(reinterpret_cast<CUcontext>(gpu_->native_context_handle())) != CUDA_SUCCESS) {
        reason = "failed to select owning CUDA context for image cache";
        return nullptr;
    }

    const CudaImageKey key{digest, options, width, height};
    std::lock_guard lock(mutex_);
    if (const auto it = cuda_images_.find(key); it != cuda_images_.end()) {
        cache_hit = true;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->cuda_image_cache_hit
                .fetch_add(1, std::memory_order_relaxed);
        }
        return it->second;
    }
    if (profiling::g_current_counters) {
        profiling::g_current_counters->cuda_image_cache_miss
            .fetch_add(1, std::memory_order_relaxed);
    }

    auto resource = std::make_shared<CudaImageResource>();
    resource->owner_gpu = gpu_;
    const auto image_desc = runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent);
    const std::size_t expected = image_desc.bytes;
    if (rgba.size_bytes() != expected) {
        reason = "CUDA image payload size does not match dimensions";
        return nullptr;
    }
    const auto started = std::chrono::steady_clock::now();
    if (cuMemAlloc(&resource->ptr, expected) != CUDA_SUCCESS ||
        cuMemcpyHtoD(resource->ptr, rgba.data(), expected) != CUDA_SUCCESS) {
        reason = "failed to upload image into CUDA resident cache";
        return nullptr;
    }
    const auto finished = std::chrono::steady_clock::now();
    upload_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    resource->width = width;
    resource->height = height;
    resource->pitch_bytes = image_desc.bytes / height;
    std::shared_ptr<const CudaImageResource> published = std::move(resource);
    cuda_images_.emplace(key, published);
    return published;
}
#endif

} // namespace chronon3d::media
