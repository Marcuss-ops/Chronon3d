// Native CUDA video benchmark: NVDEC -> CUDA NV12 composite -> NVENC.
// Overlay layers are small RGBA device buffers uploaded once.  The video
// frame never enters host memory after decode.
// Split (each part ≤ 400 LOC); the anonymous-namespace helpers live in:
//   - cuda_nvdec_nvenc_overlay_bench_core.inc        (overlay/kernel/ring)
//   - cuda_nvdec_nvenc_overlay_bench_decode_runs.inc (NVDEC ceiling modes)
//   - cuda_nvdec_nvenc_overlay_bench_encode_runs.inc (NVENC ceiling modes)
#include <cuda.h>
#include <nvrtc.h>
#ifdef CHRONON3D_ENABLE_RAW_NVENC
#include <nvEncodeAPI.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cuda_nvdec_nvenc_overlay_bench_core.inc"
#include "cuda_nvdec_nvenc_overlay_bench_decode_runs.inc"
#include "cuda_nvdec_nvenc_overlay_bench_encode_runs.inc"

int main(int argc, char** argv) {
  av_log_set_level(AV_LOG_ERROR);
  if (argc >= 2 && std::string(argv[1]) == "--demux-only") {
    return run_demux_only(argv[2]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--decode-only-warm") {
    return run_decode_only_warm(argv[2]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--decode-only") {
    return run_decode_only(argv[2]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--gpu-overlay-only") {
    return run_gpu_overlay_only(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5], std::atoi(argv[6]), std::atoi(argv[7]), argv[8], std::atoi(argv[9]), std::atoi(argv[10]));
  }
  if (argc >= 2 && std::string(argv[1]) == "--gpu-only") {
    return run_gpu_only(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));
  }
  if (argc >= 2 && std::string(argv[1]) == "--encode-only-warm") {
    return run_encode_only_warm(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--encode-only-async") {
    return run_encode_only_async(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--encode-only") {
    return run_encode_only(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
  }

  if (argc != 13) {
    std::fprintf(stderr, "usage: %s input.mp4 output.mp4 wm.rgba wm_w wm_h wm_x wm_y sub.rgba sub_w sub_h sub_x sub_y\n", argv[0]);
    return 2;
  }
  try {
    auto process_start = Clock::now();
    auto startup_start = Clock::now();

    const char* input = argv[1]; const char* output = argv[2];
    const int ww = std::atoi(argv[4]), wh = std::atoi(argv[5]), wx = std::atoi(argv[6]), wy = std::atoi(argv[7]);
    const int sw = std::atoi(argv[9]), sh = std::atoi(argv[10]), sx = std::atoi(argv[11]), sy = std::atoi(argv[12]);
    CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev,0), "cuDeviceGet");
    cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
    cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
    cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
    CUstream stream{}; cu_ok(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    const auto wm_host = read_rgba(argv[3], ww, wh), sub_host = read_rgba(argv[8], sw, sh);
    Layer wm{.ptr=0, .w=ww, .h=wh, .x=wx, .y=wy, .opacity=0.75f};
    Layer sub{.ptr=0, .w=sw, .h=sh, .x=sx, .y=sy, .opacity=1.0f};
    cu_ok(cuMemAlloc(&wm.ptr, wm_host.size()), "cuMemAlloc(wm)"); cu_ok(cuMemAlloc(&sub.ptr, sub_host.size()), "cuMemAlloc(sub)");
    cu_ok(cuMemcpyHtoD(wm.ptr, wm_host.data(), wm_host.size()), "cuMemcpyHtoD(wm)"); cu_ok(cuMemcpyHtoD(sub.ptr, sub_host.data(), sub_host.size()), "cuMemcpyHtoD(sub)");

    struct GpuOverlayItemHost {
      const unsigned char* ptr;
      int w;
      int h;
      int x;
      int y;
      float opacity;
    };
    std::vector<GpuOverlayItemHost> layer_items = {
      {reinterpret_cast<const unsigned char*>(wm.ptr), wm.w, wm.h, wm.x, wm.y, wm.opacity},
      {reinterpret_cast<const unsigned char*>(sub.ptr), sub.w, sub.h, sub.x, sub.y, sub.opacity}
    };
    CUdeviceptr gpu_layers_table{};
    cu_ok(cuMemAlloc(&gpu_layers_table, layer_items.size() * sizeof(GpuOverlayItemHost)), "cuMemAlloc(layer_items)");
    cu_ok(cuMemcpyHtoD(gpu_layers_table, layer_items.data(), layer_items.size() * sizeof(GpuOverlayItemHost)), "cuMemcpyHtoD(layer_items)");
    int layer_count = static_cast<int>(layer_items.size());

    std::vector<char> ptx;
    const char* ptx_cache = std::getenv("CHRONON_CUDA_PTX_CACHE");
    if (ptx_cache && *ptx_cache) {
      std::ifstream cached(ptx_cache, std::ios::binary | std::ios::ate);
      if (cached) {
        const auto size = cached.tellg();
        if (size > 0) { ptx.resize(static_cast<std::size_t>(size)); cached.seekg(0); cached.read(ptx.data(), size); }
      }
    }
    if (ptx.empty()) {
      nvrtcProgram prog{}; nv_ok(nvrtcCreateProgram(&prog,kKernel,"composite.cu",0,nullptr,nullptr),"nvrtcCreateProgram");
      const char* opts[] = {"--gpu-architecture=compute_75"};
      const nvrtcResult compile_result = nvrtcCompileProgram(prog,1,opts);
      if (compile_result != NVRTC_SUCCESS) {
        size_t log_size{}; nvrtcGetProgramLogSize(prog,&log_size); std::string log(log_size,'\0');
        if (log_size) nvrtcGetProgramLog(prog,log.data());
        nvrtcDestroyProgram(&prog); fail(std::string("nvrtcCompileProgram: ") + nvrtcGetErrorString(compile_result) + "\n" + log);
      }
      size_t ptx_size{}; nv_ok(nvrtcGetPTXSize(prog,&ptx_size),"nvrtcGetPTXSize"); ptx.resize(ptx_size); nv_ok(nvrtcGetPTX(prog,ptx.data()),"nvrtcGetPTX"); nvrtcDestroyProgram(&prog);
      if (ptx_cache && *ptx_cache) { std::ofstream cached(ptx_cache, std::ios::binary | std::ios::trunc); if (cached) cached.write(ptx.data(), static_cast<std::streamsize>(ptx.size())); }
    }
    CUmodule mod{}; CUfunction kernel{}; cu_ok(cuModuleLoadData(&mod,ptx.data()),"cuModuleLoadData"); cu_ok(cuModuleGetFunction(&kernel,mod,"fused_nv12_composite"),"cuModuleGetFunction");

    AVFormatContext* in{}; av_ok(avformat_open_input(&in,input,nullptr,nullptr),"avformat_open_input"); av_ok(avformat_find_stream_info(in,nullptr),"avformat_find_stream_info");
    const int si = av_find_best_stream(in,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0); if(si<0) fail("video stream missing");
    const char* decoder_name = std::getenv("CHRONON_CUDA_DECODER");
    const AVCodec* dec = (decoder_name && *decoder_name) ? avcodec_find_decoder_by_name(decoder_name) : avcodec_find_decoder_by_name("h264_cuvid");
    if (!dec) dec = avcodec_find_decoder(in->streams[si]->codecpar->codec_id);
    if (!dec) fail("decoder missing");
    AVCodecContext* dc = avcodec_alloc_context3(dec); if(!dc) fail("decoder alloc"); av_ok(avcodec_parameters_to_context(dc,in->streams[si]->codecpar),"decoder parameters"); dc->thread_count = 1; dc->thread_type = 0;
    dc->get_format = hw_format;
    dc->extra_hw_frames = 8;
    AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev,AV_HWDEVICE_TYPE_CUDA,"0",nullptr,AV_CUDA_USE_PRIMARY_CONTEXT),"av_hwdevice_ctx_create"); dc->hw_device_ctx=av_buffer_ref(hwdev); av_ok(avcodec_open2(dc,dec,nullptr),"avcodec_open2");
    const int W=dc->width, H=dc->height; if(W<=0||H<=0) fail("invalid dimensions");

    AVFormatContext* out{}; avformat_alloc_output_context2(&out,nullptr,nullptr,output); if(!out) fail("output alloc");
    const AVCodec* enc_codec=avcodec_find_encoder_by_name("h264_nvenc"); if(!enc_codec) fail("h264_nvenc missing"); AVStream* st=avformat_new_stream(out,enc_codec); if(!st) fail("stream alloc");
    const int nvenc_surfaces = std::max(1, std::atoi(std::getenv("CHRONON_NVENC_SURFACES") ?: "2"));
    AVCodecContext* ec=avcodec_alloc_context3(enc_codec); if(!ec) fail("encoder alloc"); ec->width=W; ec->height=H; ec->time_base={1,24}; ec->framerate={24,1}; ec->pix_fmt=AV_PIX_FMT_CUDA; ec->bit_rate=0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY; av_opt_set(ec->priv_data,"preset","p1",0); av_opt_set_int(ec->priv_data,"qp",23,0); av_opt_set_int(ec->priv_data,"surfaces",nvenc_surfaces,0); av_opt_set_int(ec->priv_data,"zerolatency",1,0); av_opt_set_int(ec->priv_data,"b_adapt",0,0);
    AVBufferRef* frames=av_hwframe_ctx_alloc(hwdev); if(!frames) fail("frames alloc"); auto* fctx=(AVHWFramesContext*)frames->data; fctx->format=AV_PIX_FMT_CUDA; fctx->sw_format=AV_PIX_FMT_NV12; fctx->width=W; fctx->height=H; fctx->initial_pool_size=16; av_ok(av_hwframe_ctx_init(frames),"av_hwframe_ctx_init"); ec->hw_frames_ctx=av_buffer_ref(frames); av_ok(avcodec_open2(ec,enc_codec,nullptr),"encoder open"); av_ok(avcodec_parameters_from_context(st->codecpar,ec),"codec parameters"); st->time_base=ec->time_base;
    if(!(out->oformat->flags&AVFMT_NOFILE)) av_ok(avio_open(&out->pb,output,AVIO_FLAG_WRITE),"avio_open"); av_ok(avformat_write_header(out,nullptr),"avformat_write_header");

    const size_t kRingSlots = 16;
    FrameSlotRing slot_ring(kRingSlots, frames, W, H);

    struct QueueItem {
      FrameSlot* slot{nullptr};
      uint64_t frame_id{0};
      bool is_flush{false};
    };

    std::queue<QueueItem> encode_q;
    std::mutex q_mtx;
    std::condition_variable q_cv;
    uint64_t encoded_packet_count = 0;

    std::thread drain_thread([&]() {
      AVPacket* local_pkt = av_packet_alloc();
      while (true) {
        QueueItem item{};
        {
          std::unique_lock<std::mutex> lock(q_mtx);
          q_cv.wait(lock, [&]() { return !encode_q.empty(); });
          item = encode_q.front();
          encode_q.pop();
        }
        if (item.is_flush) {
          write_packets(ec, out, local_pkt, nullptr, encoded_packet_count);
          break;
        }

        auto t_ev0 = Clock::now();
        cuEventSynchronize(item.slot->gpu_ready_ev);
        auto t_ev1 = Clock::now();
        g_timings.gpu_event_wait_ms += std::chrono::duration<double, std::milli>(t_ev1 - t_ev0).count();

        float gpu_item_ms = 0.0f;
        cuEventElapsedTime(&gpu_item_ms, item.slot->gpu_start_timing_ev, item.slot->gpu_end_timing_ev);
        g_timings.gpu_execute_ms += gpu_item_ms;

        item.slot->frame->pts = static_cast<int64_t>(item.frame_id);
        write_packets(ec, out, local_pkt, item.slot->frame, encoded_packet_count);

        // Explicit handshake: release slot back to producer ring
        slot_ring.release_slot(item.slot);
      }
      av_packet_free(&local_pkt);
    });

    auto startup_end = Clock::now();
    g_timings.startup_ms = std::chrono::duration<double, std::milli>(startup_end - startup_start).count();

    // WARM PIPELINE START
    auto pipeline_start = Clock::now();

    uint64_t submitted_frame_id = 0;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* decoded = av_frame_alloc();
    if (!pkt || !decoded) fail("frame/packet alloc");

    auto process = [&](AVFrame* src) {
      FrameSlot* slot = slot_ring.acquire_free_slot();
      auto t_k0 = Clock::now();

      int dst_yp = slot->frame->linesize[0], dst_uvp = slot->frame->linesize[1];
      int src_yp = src->linesize[0], src_uvp = src->linesize[1];
      int kernel_w = W, kernel_h = H;
      void* args[] = {
        &slot->frame->data[0], &slot->frame->data[1], &dst_yp, &dst_uvp,
        &src->data[0], &src->data[1], &src_yp, &src_uvp,
        &gpu_layers_table, &layer_count,
        &kernel_w, &kernel_h
      };

      cu_ok(cuEventRecord(slot->gpu_start_timing_ev, stream), "cuEventRecord start");
      cu_ok(cuLaunchKernel(kernel, (W+31)/32, (H+31)/32, 1, 32, 16, 1, 0, stream, args, nullptr), "cuLaunchKernel fused");
      cu_ok(cuEventRecord(slot->gpu_end_timing_ev, stream), "cuEventRecord end");
      cu_ok(cuEventRecord(slot->gpu_ready_ev, stream), "cuEventRecord ready");

      auto t_k1 = Clock::now();
      g_timings.gpu_submit_ms += std::chrono::duration<double, std::milli>(t_k1 - t_k0).count();

      const uint64_t cur_frame_id = submitted_frame_id++;
      slot_ring.mark_ready(slot);

      {
        std::lock_guard<std::mutex> lock(q_mtx);
        encode_q.push({slot, cur_frame_id, false});
        if (encode_q.size() > g_timings.queue_high_watermark) {
          g_timings.queue_high_watermark = encode_q.size();
        }
      }
      q_cv.notify_one();
    };

    while (true) {
      auto t_dm0 = Clock::now();
      int r = av_read_frame(in, pkt);
      auto t_dm1 = Clock::now();
      g_timings.demux_ms += std::chrono::duration<double, std::milli>(t_dm1 - t_dm0).count();
      if (r < 0) break;

      if (pkt->stream_index == si) {
        auto t_dec_send0 = Clock::now();
        av_ok(avcodec_send_packet(dc, pkt), "avcodec_send_packet");
        auto t_dec_send1 = Clock::now();
        g_timings.decode_send_ms += std::chrono::duration<double, std::milli>(t_dec_send1 - t_dec_send0).count();

        while (true) {
          auto t_dec_rec0 = Clock::now();
          int rec_r = avcodec_receive_frame(dc, decoded);
          auto t_dec_rec1 = Clock::now();
          g_timings.decode_receive_ms += std::chrono::duration<double, std::milli>(t_dec_rec1 - t_dec_rec0).count();
          if (rec_r < 0) break;
          process(decoded);
        }
      }
      av_packet_unref(pkt);
    }

    auto t_flush0 = Clock::now();
    avcodec_send_packet(dc, nullptr);
    while (true) {
      auto t_dec_rec0 = Clock::now();
      int rec_r = avcodec_receive_frame(dc, decoded);
      auto t_dec_rec1 = Clock::now();
      g_timings.decode_receive_ms += std::chrono::duration<double, std::milli>(t_dec_rec1 - t_dec_rec0).count();
      if (rec_r < 0) break;
      process(decoded);
    }
    auto t_flush1 = Clock::now();
    g_timings.decode_send_ms += std::chrono::duration<double, std::milli>(t_flush1 - t_flush0).count();

    {
      std::lock_guard<std::mutex> lock(q_mtx);
      encode_q.push({nullptr, 0, true});
    }
    q_cv.notify_one();
    if (drain_thread.joinable()) drain_thread.join();

    auto t_tr0 = Clock::now();
    av_write_trailer(out);
    auto t_tr1 = Clock::now();
    g_timings.trailer_ms = std::chrono::duration<double, std::milli>(t_tr1 - t_tr0).count();

    auto pipeline_end = Clock::now();
    g_timings.pipeline_wall_ms = std::chrono::duration<double, std::milli>(pipeline_end - pipeline_start).count();

    // TEARDOWN START
    auto teardown_start = Clock::now();
    if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
    av_frame_free(&decoded);
    av_packet_free(&pkt);
    avcodec_free_context(&ec);
    avcodec_free_context(&dc);
    avformat_close_input(&in);
    avformat_free_context(out);
    av_buffer_unref(&frames);
    av_buffer_unref(&hwdev);
    cuMemFree(wm.ptr);
    cuMemFree(sub.ptr);
    cuMemFree(gpu_layers_table);
    cuModuleUnload(mod);
    cuStreamDestroy(stream);
    cuDevicePrimaryCtxRelease(dev);
    auto teardown_end = Clock::now();
    g_timings.teardown_ms = std::chrono::duration<double, std::milli>(teardown_end - teardown_start).count();

    auto process_end = Clock::now();
    g_timings.process_wall_ms = std::chrono::duration<double, std::milli>(process_end - process_start).count();

    const double frames_n = static_cast<double>(submitted_frame_id);
    std::cout << "\n================ COMPLETE BENCHMARK TIMING BREAKDOWN (" << submitted_frame_id << " frames) ================\n"
              << "  [1] Startup & Context Init:   " << g_timings.startup_ms << " ms\n"
              << "  [2] Demux (av_read_frame):    " << g_timings.demux_ms << " ms  (" << (g_timings.demux_ms / frames_n) << " ms/f)\n"
              << "  [3] Decode Send Packet:       " << g_timings.decode_send_ms << " ms  (" << (g_timings.decode_send_ms / frames_n) << " ms/f)\n"
              << "  [4] Decode Receive Frame:     " << g_timings.decode_receive_ms << " ms  (" << (g_timings.decode_receive_ms / frames_n) << " ms/f)\n"
              << "  [5] GPU Kernel CPU Submit:    " << g_timings.gpu_submit_ms << " ms  (" << (g_timings.gpu_submit_ms / frames_n) << " ms/f)\n"
              << "  [6] Pure GPU Execution (Ev):  " << g_timings.gpu_execute_ms << " ms  (" << (g_timings.gpu_execute_ms / frames_n) << " ms/f)\n"
              << "  [7] GPU Event Wait (Drain):   " << g_timings.gpu_event_wait_ms << " ms  (" << (g_timings.gpu_event_wait_ms / frames_n) << " ms/f)\n"
              << "  [8] Encode Send Frame:        " << g_timings.encode_send_ms << " ms  (" << (g_timings.encode_send_ms / frames_n) << " ms/f)\n"
              << "  [9] Encode Receive Packet:    " << g_timings.encode_receive_ms << " ms  (" << (g_timings.encode_receive_ms / frames_n) << " ms/f)\n"
              << " [10] Mux Write Interleaved:    " << g_timings.mux_write_ms << " ms  (" << (g_timings.mux_write_ms / frames_n) << " ms/f)\n"
              << " [11] Queue Backpressure Wait:  " << g_timings.queue_wait_ms << " ms\n"
              << " [12] Trailer Write:            " << g_timings.trailer_ms << " ms\n"
              << " [13] Teardown & Free:          " << g_timings.teardown_ms << " ms\n"
              << " ---------------------------------------------------------------------------------\n"
              << "  >>> WARM PIPELINE WALL TIME:  " << g_timings.pipeline_wall_ms << " ms  ==> " << (1000.0 * frames_n / g_timings.pipeline_wall_ms) << " FPS\n"
              << "  >>> COLD PROCESS WALL TIME:   " << g_timings.process_wall_ms << " ms  ==> " << (1000.0 * frames_n / g_timings.process_wall_ms) << " FPS\n"
              << "  >>> Queue High-Watermark:     " << g_timings.queue_high_watermark << " / " << kRingSlots << " slots\n"
              << "==================================================================================\n\n";

    std::cout << "CUDA_NVDEC_NVENC_OVERLAY_PASS decoder=" << dec->name << " surfaces=" << nvenc_surfaces << " output=" << output << " frames=" << submitted_frame_id << "\n";
    return 0;
  } catch(const std::exception& e){ std::cerr << "CUDA_NVDEC_NVENC_OVERLAY_FAIL: " << e.what() << "\n"; return 1; }
}
