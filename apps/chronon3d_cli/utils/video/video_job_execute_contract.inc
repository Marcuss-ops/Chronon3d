namespace {

// Build the target-side portion of the bitstream contract from the same
// export options that will be handed to the encoder.  Fields that are only
// known after encoder initialisation (profile/level/extradata) deliberately
// remain unknown; inspect_gop_source then fails closed until a concrete
// encoder contract is supplied.
BitstreamTargetContract make_bitstream_target_contract(
    const FfmpegExportOptions& opts, const CompiledComposition& compiled,
    const std::string& source_path) {
    BitstreamTargetContract target;
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    if (opts.encoder.codec == "h264" || opts.encoder.codec == "libx264" ||
        opts.encoder.codec == "h264_nvenc") {
        target.codec = AV_CODEC_ID_H264;
    } else if (opts.encoder.codec == "hevc" || opts.encoder.codec == "h265" ||
               opts.encoder.codec == "libx265" || opts.encoder.codec == "hevc_nvenc") {
        target.codec = AV_CODEC_ID_HEVC;
    } else if (opts.encoder.codec == "av1" || opts.encoder.codec == "av1_nvenc") {
        target.codec = AV_CODEC_ID_AV1;
    }
    target.width = static_cast<std::uint32_t>(compiled.composition->width());
    target.height = static_cast<std::uint32_t>(compiled.composition->height());
    target.pixel_format = opts.encoder.encoder_backend == "native" &&
            opts.encoder.hardware_encoder == "nvenc"
        ? AV_PIX_FMT_CUDA : AV_PIX_FMT_YUV420P;

    // Packet-copy output preserves the source codec parameters exactly. Use
    // those parameters as the target contract for the source-preserving mux;
    // this is the only valid target before a re-encoder has been opened. A
    // failed probe leaves the configured fields above and therefore remains
    // fail-closed in compare_bitstream_compatibility().
    AVFormatContext* format = nullptr;
    if (!source_path.empty() &&
        avformat_open_input(&format, source_path.c_str(), nullptr, nullptr) >= 0 &&
        avformat_find_stream_info(format, nullptr) >= 0) {
        for (unsigned int index = 0; index < format->nb_streams; ++index) {
            const auto* params = format->streams[index]->codecpar;
            if (params->codec_type != AVMEDIA_TYPE_VIDEO) continue;
            target.codec = params->codec_id;
            target.profile = params->profile;
            target.level = params->level;
            target.width = static_cast<std::uint32_t>(params->width);
            target.height = static_cast<std::uint32_t>(params->height);
            target.pixel_format = static_cast<BitstreamPixelFormat>(params->format);
            if (params->extradata && params->extradata_size > 0) {
                target.parameter_sets.assign(
                    params->extradata, params->extradata + params->extradata_size);
            }
            target.color_range = params->color_range;
            target.color_space = params->color_space;
            target.color_primaries = params->color_primaries;
            target.color_trc = params->color_trc;
            break;
        }
        avformat_close_input(&format);
    }
#else
    (void)opts;
    (void)compiled;
    (void)source_path;
#endif
    return target;
}

} // namespace
