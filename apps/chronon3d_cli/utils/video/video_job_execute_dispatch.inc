int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoJobExecutionContext> execution)
{
    if (opts.output.output.empty()) {
        spdlog::error("[video] No output path specified.");
        return 1;
    }
    if (encoder_backend_requires_ffmpeg(opts.encoder.encoder_backend) &&
        !ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH (required by the pipe encoder).");
        return 1;
    }
    if (end <= start) {
        spdlog::error("[video] Empty frame range [{}, {})", start, end);
        return 1;
    }
    if (opts.sink.ffmpeg_mode != "pipe") {
        spdlog::error("[video] Unsupported ffmpeg mode '{}'. Only 'pipe' is available",
                      opts.sink.ffmpeg_mode);
        return 1;
    }

    // ── Whole-clip BitstreamCopy dispatch ──────────────────────────────
    // The canonical execution resolver decides the path before any renderer
    // or encoder is created. When it selects BitstreamCopy the clip has no
    // visual modifications (no watermark, no subtitles, no background) and
    // the source codec is compatible: the compressed bitstream is copied
    // packet-by-packet directly into the output container. NVDEC, CUDA
    // compositing and NVENC are never initialised — zero GPU work.
    //
    // This dispatch MUST stay above the pipe exporter call. The pipe
    // exporter's setup_pipe_export_session() also calls the resolver but
    // treats BitstreamCopy as an error ("bitstream-copy execution must use
    // the packet pipeline") because the pipe renderer cannot do packet
    // copy. Routing here ensures copy_gop_source() is the single entry.
    if (opts.sink.ffmpeg_mode == "pipe") {
        const auto path_decision = media::detail::resolve_legacy_video_execution(
            media::detail::LegacyVideoExecutionRequest{
                .encoder_backend = opts.encoder.encoder_backend,
                .hardware_encoder = opts.encoder.hardware_encoder,
                .codec = opts.encoder.codec,
                .hot_path = opts.gpu_hot_path_mode,
                .has_gop_source = !opts.gop_source.empty(),
                .gop_copy_only = opts.gop_copy_only,
                .allow_hybrid_gop = !opts.gop_source.empty() &&
                    !opts.gop_copy_only});
        if (path_decision.path == media::VideoExecutionPath::BitstreamCopy &&
            path_decision.valid) {
            // Explicit whole-clip copy is only an intent. Validate the
            // source before creating any renderer or encoder. The packet
            // preflight is fail-closed: an unavailable/incompatible source
            // never gets copied; it is routed to DirectYuv instead.
            const double fps_val = opts.output.fps_value();
            const double start_sec = static_cast<double>(start.integral()) /
                (fps_val > 0.0 ? fps_val : 30.0);
            const double end_sec = static_cast<double>(end.integral()) /
                (fps_val > 0.0 ? fps_val : 30.0);
            const auto analysis = inspect_gop_source(
                opts.gop_source, make_bitstream_target_contract(opts, compiled,
                                                                opts.gop_source), start_sec, end_sec);
            if (!analysis || !analysis->all_copy_eligible) {
                spdlog::warn("[bitstream-copy] compatibility preflight failed; "
                             "falling back to DirectYuv before renderer setup");
                // Re-enter the render path with an explicit DirectYuv
                // decision. Do not retry BitstreamCopy and do not let the
                // lower-level exporter infer a path.
                auto fallback_opts = opts;
                fallback_opts.gop_copy_only = false;
                // Keep gop_source on the explicit DirectYuv fallback: the
                // native A/V mux uses the same materialized source to copy
                // the master audio stream. Clearing it here made a valid
                // video render silently lose audio after a GOP preflight
                // downgrade.
                fallback_opts.resolved_execution_path =
                    FfmpegExportOptions::ResolvedExecutionPath::DirectYuv;
                fallback_opts.resolved_execution_plan = media::detail::resolve_legacy_video_execution(
                    media::detail::LegacyVideoExecutionRequest{
                        .encoder_backend = opts.encoder.encoder_backend,
                        .hardware_encoder = opts.encoder.hardware_encoder,
                        .codec = opts.encoder.codec,
                        .hot_path = opts.gpu_hot_path_mode}).plan;
                auto fallback = render_and_encode_ffmpeg_pipe(
                    registry, compiled, composition_id, settings,
                    start, end, fallback_opts, cpu_budget,
                    nullptr, nullptr, std::move(execution));
                return fallback.return_code;
            }
            spdlog::info("[bitstream-copy] path selected (reason: {}); "
                         "bypassing NVDEC/CUDA/NVENC entirely",
                         path_decision.reason);
            const auto copy_t0 = profiling::now();

            // The source timeline uses its own time_base; copy_gop_source
            // converts internally. A frame range [start, end) maps to
            // [start/fps, end/fps) in seconds.

            // Ensure the output directory exists.
            if (const auto parent = std::filesystem::path(opts.output.output)
                    .parent_path(); !parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    spdlog::error("[bitstream-copy] failed to create output "
                                  "directory '{}': {}",
                                  parent.string(), ec.message());
                    return 1;
                }
            }

            // Atomic output: write to .partial then rename on success.
            auto final_path = std::filesystem::path(opts.output.output);
            auto partial_path = final_path;
            const auto ext = final_path.extension();
            if (ext.empty()) {
                partial_path += ".partial";
            } else {
                partial_path.replace_filename(
                    final_path.stem().string() + ".partial" + ext.string());
            }

            auto copy_result = copy_gop_source(
                opts.gop_source,
                partial_path.string(),
                start_sec, end_sec);
            const auto copy_t1 = profiling::now();
            const double copy_ms = profiling::duration_ms(copy_t0, copy_t1);

            if (!copy_result) {
                spdlog::error("[bitstream-copy] copy_gop_source failed for "
                              "source '{}' → '{}'",
                              opts.gop_source, partial_path.string());
                std::error_code rm_ec;
                std::filesystem::remove(partial_path, rm_ec);
                return 1;
            }

            // Atomic rename: .partial → final.
            std::error_code rename_ec;
            std::filesystem::rename(partial_path, final_path, rename_ec);
            if (rename_ec) {
                spdlog::error("[bitstream-copy] failed to rename '{}' → '{}': {}",
                              partial_path.string(), final_path.string(),
                              rename_ec.message());
                return 1;
            }

            spdlog::info("[bitstream-copy] completed: {} video packets, "
                         "{} audio packets in {:.1f}ms → {}",
                         copy_result->video_packets,
                         copy_result->audio_packets,
                         copy_ms, opts.output.output);
            return 0;  // success — no render/encode needed
        }

        // ── Smart GOP hybrid dispatch ──────────────────────────────────
        // The resolver selected SmartGopCopy: the source has a compressed
        // bitstream and some GOPs may be eligible for packet copy while
        // others need DirectYUV/NVENC re-encode. The plan is produced by
        // inspect_gop_source() at execution time.
        //
        // Current implementation: inspect the source to determine if ALL
        // GOPs are copy-eligible (whole-clip copy fast path) or if the
        // plan is truly hybrid. When hybrid, the safe_to_splice gate
        // must pass; otherwise we downgrade to DirectYuv for the entire
        // clip (the pipe exporter handles it).
        if (path_decision.path == media::VideoExecutionPath::SmartGopCopy &&
            path_decision.valid) {
            const double fps_val = opts.output.fps_value();
            const double start_sec = static_cast<double>(start.integral()) /
                (fps_val > 0.0 ? fps_val : 30.0);
            const double end_sec = static_cast<double>(end.integral()) /
                (fps_val > 0.0 ? fps_val : 30.0);

            spdlog::info("[smart-gop] inspecting source '{}' [{:.2f}s, {:.2f}s)",
                         opts.gop_source, start_sec, end_sec);
            auto analysis = inspect_gop_source(
                opts.gop_source, make_bitstream_target_contract(opts, compiled,
                                                                opts.gop_source),
                start_sec, end_sec);

            if (analysis && analysis->all_copy_eligible) {
                // All GOPs are clean and codec-compatible → whole-clip
                // copy, zero GPU. Same fast path as BitstreamCopy.
                spdlog::info("[smart-gop] all {} GOPs copy-eligible; "
                             "routing to whole-clip copy",
                             analysis->plans.size());
                auto final_path = std::filesystem::path(opts.output.output);
                auto partial_path = final_path;
                const auto ext = final_path.extension();
                if (ext.empty()) {
                    partial_path += ".partial";
                } else {
                    partial_path.replace_filename(
                        final_path.stem().string() + ".partial" + ext.string());
                }
                if (const auto parent = final_path.parent_path(); !parent.empty()) {
                    std::error_code ec;
                    std::filesystem::create_directories(parent, ec);
                }
                const auto copy_t0 = profiling::now();
                auto copy_result = copy_gop_source(
                    opts.gop_source, partial_path.string(),
                    start_sec, end_sec);
                const double copy_ms = profiling::duration_ms(copy_t0, profiling::now());
                if (!copy_result) {
                    std::error_code rm_ec;
                    std::filesystem::remove(partial_path, rm_ec);
                    spdlog::error("[smart-gop] copy_gop_source failed");
                    return 1;
                }
                std::error_code rename_ec;
                std::filesystem::rename(partial_path, final_path, rename_ec);
                if (rename_ec) {
                    spdlog::error("[smart-gop] rename failed: {}",
                                  rename_ec.message());
                    return 1;
                }
                spdlog::info("[smart-gop] whole-clip copy: {} video packets, "
                             "{} audio packets in {:.1f}ms",
                             copy_result->video_packets,
                             copy_result->audio_packets, copy_ms);
                return 0;
            }

            if (analysis && analysis->is_hybrid()) {
                // True hybrid: some GOPs copy, others re-encode. This
                // requires the unified PacketAssembler pipeline that can
                // accept both copied and NVENC-encoded packets.
                //
                // FAIL_CLOSED: the BitstreamCompatibility gate must pass
                // for every copy-eligible GOP. If any GOP's gate fails
                // (codec/profile/level/SPS/PPS mismatch), splicing source
                // H.264 + NVENC H.264 would corrupt the decoder (green
                // frames, seek errors, player-specific crashes). We
                // downgrade to DirectYuv for the ENTIRE clip — correct
                // but not optimal. The full hybrid pipeline (per-GOP demux
                // + selective NVDEC/NVENC + PacketAssembler merge) is a
                // future step; until then the safe fallback guarantees a
                // valid output.
                bool all_safe = true;
                for (const auto& plan : analysis->plans) {
                    if (plan.copy_packets() &&
                        !plan.compatibility.safe_to_splice()) {
                        all_safe = false;
                        spdlog::warn("[smart-gop] GOP [{}, {}] copy-eligible "
                                     "but BitstreamCompatibility gate FAILED "
                                     "— splicing source+NVENC would corrupt",
                                     plan.first_pts, plan.last_pts);
                        break;
                    }
                }
                if (!all_safe) {
                    spdlog::info("[smart-gop] BitstreamCompatibility gate "
                                 "failed; downgrading to DirectYuv for the "
                                 "entire clip (fail-closed)");
                    // Fall through to the pipe exporter (DirectYuv).
                } else {
                    spdlog::info("[smart-gop] hybrid plan: {}/{} GOPs copy, "
                                 "{}/{} re-encode; splice_safe=true; "
                                 "downgrading to DirectYuv "
                                 "(hybrid pipeline TBD)",
                                 analysis->copy_count, analysis->plans.size(),
                                 analysis->reencode_count,
                                 analysis->plans.size());
                    // Fall through to the pipe exporter (DirectYuv path).
                }
            } else {
                // No GOPs are copy-eligible (all touched or codec
                // mismatch). DirectYuv for the entire clip is the
                // correct path.
                spdlog::info("[smart-gop] no copy-eligible GOPs; "
                             "routing to DirectYuv");
                // Fall through to the pipe exporter.
            }
        }

        // The resolver has already dispatched all packet-copy paths above.
        // Only DirectYuv and FullGraph are allowed to enter the renderer
        // session builder; it must never resolve the path again.
        auto render_opts = opts;
        const auto render_path =
            path_decision.path == media::VideoExecutionPath::DirectYuv
                ? media::VideoExecutionPath::DirectYuv
                : path_decision.render_fallback;
        render_opts.resolved_execution_path =
            render_path == media::VideoExecutionPath::DirectYuv
                ? FfmpegExportOptions::ResolvedExecutionPath::DirectYuv
                : FfmpegExportOptions::ResolvedExecutionPath::FullGraph;
        render_opts.resolved_execution_plan = path_decision.plan;
        auto result = render_and_encode_ffmpeg_pipe(
            registry, compiled, composition_id,
            settings, start, end, render_opts, cpu_budget,
            nullptr, nullptr, std::move(execution));
        return result.return_code;
    }
    return 1;
}
