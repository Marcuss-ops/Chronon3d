#include "native_video_frame_decoder_detail.hpp"
#ifdef CHRONON3D_ENABLE_OCIO
#include <chronon3d/color/ocio_color_transform.hpp>
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

namespace chronon3d::media {

DecodeFailure lookup_failure(SampleLookupDisposition disposition,
                             RationalTime requested_time) {
    DecodeFailureReason reason = DecodeFailureReason::PresentationGap;
    const char* message = "presentation time falls inside a source timeline gap";
    if (disposition == SampleLookupDisposition::BeforeStart) {
        reason = DecodeFailureReason::BeforeStart;
        message = "presentation time is before the first source sample";
    }
    return make_decode_failure(DecodeDiagnostic{
        reason, 0, kNoDecodeTimestamp, kNoDecodeTimestamp, 0, message});
}

DecodedFrame decoded_frame_from_sample(std::shared_ptr<Framebuffer> framebuffer,
                                       const SourceSample& sample) {
    return DecodedFrame{std::move(framebuffer), sample.pts, sample.dts, sample.source_order};
}

DecodeResult NativeVideoFrameDecoder::decode_frame_at(
    const std::string& path, RationalTime presentation_time, int width, int height) {
    return decode_frame_at_impl(path, presentation_time, width, height, nullptr);
}

DecodeResult NativeVideoFrameDecoder::decode_frame_at_with_color_transform(
    const std::string& path, RationalTime presentation_time, int width, int height,
    const VideoColorTransform& transform) {
    if (!transform.enabled())
        return decode_frame_at(path, presentation_time, width, height);
#ifndef CHRONON3D_ENABLE_OCIO
    return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::UnsupportedFormatChange, 0,
        kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
        "video declares an OCIO input space, but Chronon was built without OpenColorIO"});
#else
    return decode_frame_at_impl(path, presentation_time, width, height, &transform);
#endif
}

DecodeResult NativeVideoFrameDecoder::decode_frame_at_impl(
    const std::string& path, RationalTime presentation_time, int, int,
    const VideoColorTransform* transform_options) {
#ifdef CHRONON3D_ENABLE_OCIO
    std::shared_ptr<color::OcioColorTransform> ocio_transform;
    if (transform_options != nullptr) {
        std::error_code metadata_error;
        const auto modified = std::filesystem::last_write_time(
            transform_options->config_path, metadata_error);
        const auto size = metadata_error ? 0 : std::filesystem::file_size(
            transform_options->config_path, metadata_error);
        if (metadata_error)
            return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::UnsupportedFormatChange, 0,
                kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
                "video OCIO config could not be stat'ed: " + transform_options->config_path});
        const auto key = transform_options->config_path + "\n" +
            transform_options->input_space + "\n" + transform_options->working_space + "\n" +
            std::to_string(modified.time_since_epoch().count()) + ":" + std::to_string(size);
        {
            std::lock_guard lock(m_mutex);
            const auto it = m_ocio_transforms.find(key);
            if (it != m_ocio_transforms.end()) ocio_transform = it->second;
        }
        if (!ocio_transform) {
            std::string error;
            auto built = color::OcioColorTransform::from_config(
                transform_options->config_path, transform_options->input_space,
                transform_options->working_space, &error);
            if (!built)
                return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::UnsupportedFormatChange, 0,
                    kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
                    "video OCIO input transform failed: " + error});
            auto candidate = std::make_shared<color::OcioColorTransform>(std::move(*built));
            std::lock_guard lock(m_mutex);
            auto [it, inserted] = m_ocio_transforms.emplace(key, candidate);
            ocio_transform = inserted ? std::move(candidate) : it->second;
        }
    }
#else
    (void)transform_options;
#endif
    if (path.empty()) {
        return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::OpenInput, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "decode source path is empty"});
    }

    DecodeDiagnostic open_diagnostic;
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        session = open_session_locked(path, &open_diagnostic);
    }
    if (!session) {
        if (!open_diagnostic.failed()) {
            open_diagnostic = DecodeDiagnostic{DecodeFailureReason::DecoderUnavailable, 0,
                kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
                "decoder session could not be opened"};
        }
        return make_decode_failure(std::move(open_diagnostic));
    }

    const auto lookup = session->sample_table.lookup(presentation_time);
    if (lookup.disposition == SampleLookupDisposition::AfterEnd) {
        return DecodeEndOfStream{presentation_time};
    }
    if (!lookup.found()) {
        return lookup_failure(lookup.disposition, presentation_time);
    }
    const auto selected = *lookup.sample_index;
    const int64_t target = static_cast<int64_t>(selected);
    const auto& sample = session->sample_table[selected];

    std::unique_lock lock(session->mutex);
#ifdef CHRONON3D_ENABLE_OCIO
    if (ocio_transform) {
        session->prefetch_queue.clear();
        ++session->prefetch_generation;
        session->prefetch_next = -1;
        session->prefetch_cv.wait(lock, [&session] {
            return session->prefetch_inflight < 0 || session->prefetch_stop.load();
        });
    }
#endif
    if (session->prefetch_inflight == target) {
        session->prefetch_cv.wait(lock, [&session, target] {
            return session->prefetch_inflight != target || session->prefetch_stop.load();
        });
    }
    if (session->test_options.enable_frame_cache
#ifdef CHRONON3D_ENABLE_OCIO
        && !ocio_transform
#endif
        ) {
        if (auto cached = session->cache.get(target)) {
            return decoded_frame_from_sample(*cached, sample);
        }
    }
    while (!session->prefetch_queue.empty() && session->prefetch_queue.front().target < target) {
        session->prefetch_queue.pop_front();
    }
    if (!session->prefetch_queue.empty() && session->prefetch_queue.front().target == target
#ifdef CHRONON3D_ENABLE_OCIO
        && !ocio_transform
#endif
        ) {
        auto framebuffer = std::move(session->prefetch_queue.front().framebuffer);
        session->prefetch_queue.pop_front();
        return decoded_frame_from_sample(std::move(framebuffer), sample);
    }
    session->prefetch_queue.clear();
    ++session->prefetch_generation;
    session->prefetch_next = selected + 1 < session->sample_table.size() ? target + 1 : -1;
#ifdef CHRONON3D_ENABLE_OCIO
    if (ocio_transform) session->prefetch_next = -1;
#endif
    session->foreground_decode_pending.store(true, std::memory_order_release);
    lock.unlock();

    auto io_read = global_media_io_budget().acquire_required_read();
    auto result = decode_frame_internal(*session, target
#ifdef CHRONON3D_ENABLE_OCIO
        , ocio_transform.get()
#endif
        );

    lock.lock();
    session->foreground_decode_pending.store(false, std::memory_order_release);
    session->prefetch_cv.notify_all();
    if (auto* decoded = std::get_if<DecodedFrame>(&result)) {
        const bool native_surface = decoded->framebuffer &&
            decoded->framebuffer->surface_handle() != runtime::kInvalidRenderSurfaceHandle;
        if (!native_surface && decoded->framebuffer && session->test_options.enable_frame_cache
#ifdef CHRONON3D_ENABLE_OCIO
            && !ocio_transform
#endif
            ) {
            session->cache.put(target, decoded->framebuffer);
        }
    }
    return result;
}


} // namespace chronon3d::media

#endif
