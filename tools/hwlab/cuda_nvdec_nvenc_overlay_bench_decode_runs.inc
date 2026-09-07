namespace {

// Ceiling benchmark modes
int run_demux_only(const char* input) {
  auto start = Clock::now();
  AVFormatContext* in{}; av_ok(avformat_open_input(&in, input, nullptr, nullptr), "open");
  av_ok(avformat_find_stream_info(in, nullptr), "find_stream");
  int si = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  AVPacket* pkt = av_packet_alloc();
  uint64_t count = 0;
  while (av_read_frame(in, pkt) >= 0) {
    if (pkt->stream_index == si) count++;
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  avformat_close_input(&in);
  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING DEMUX-ONLY] frames=" << count << " total=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_decode_only_warm(const char* input) {
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* in{}; av_ok(avformat_open_input(&in, input, nullptr, nullptr), "open");
  av_ok(avformat_find_stream_info(in, nullptr), "find_stream");
  int si = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const AVCodec* dec = avcodec_find_decoder_by_name("h264_cuvid");
  if (!dec) dec = avcodec_find_decoder(in->streams[si]->codecpar->codec_id);
  AVCodecContext* dc = avcodec_alloc_context3(dec);
  av_ok(avcodec_parameters_to_context(dc, in->streams[si]->codecpar), "params");
  dc->get_format = hw_format;
  dc->extra_hw_frames = 8;
  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  dc->hw_device_ctx = av_buffer_ref(hwdev);
  av_ok(avcodec_open2(dc, dec, nullptr), "open2");

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  uint64_t count = 0;

  // WARM DECODE MEASUREMENT ONLY
  auto start = Clock::now();
  while (av_read_frame(in, pkt) >= 0) {
    if (pkt->stream_index == si) {
      av_ok(avcodec_send_packet(dc, pkt), "send");
      while (avcodec_receive_frame(dc, frame) >= 0) count++;
    }
    av_packet_unref(pkt);
  }
  avcodec_send_packet(dc, nullptr);
  while (avcodec_receive_frame(dc, frame) >= 0) count++;
  auto end = Clock::now();

  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dc);
  avformat_close_input(&in);
  av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVDEC-ONLY-WARM] decoder=" << dec->name << " frames=" << count << " warm_throughput=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_gpu_overlay_only(int count, int W, int H, const char* wm_path, int ww, int wh, const char* sub_path, int sw, int sh) {
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
  CUstream stream{}; cu_ok(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "stream");

  nvrtcProgram prog{}; nv_ok(nvrtcCreateProgram(&prog, kKernel, "composite.cu", 0, nullptr, nullptr), "createProg");
  // Canonical target for this stack is Ampere (compute_86, RTX A4000).
  const char* opts[] = {"--gpu-architecture=compute_86"};
  nv_ok(nvrtcCompileProgram(prog, 1, opts), "compileProg");
  size_t ptx_size{}; nv_ok(nvrtcGetPTXSize(prog, &ptx_size), "ptxSize");
  std::vector<char> ptx(ptx_size); nv_ok(nvrtcGetPTX(prog, ptx.data()), "getPTX"); nvrtcDestroyProgram(&prog);
  CUmodule mod{}; CUfunction kernel{}; cu_ok(cuModuleLoadData(&mod, ptx.data()), "loadData");
  cu_ok(cuModuleGetFunction(&kernel, mod, "fused_nv12_composite"), "getFunc");

  const auto wm_host = read_rgba(wm_path, ww, wh), sub_host = read_rgba(sub_path, sw, sh);
  Layer wm{.ptr=0, .w=ww, .h=wh, .x=100, .y=100, .opacity=0.75f};
  Layer sub{.ptr=0, .w=sw, .h=sh, .x=200, .y=800, .opacity=1.0f};
  cu_ok(cuMemAlloc(&wm.ptr, wm_host.size()), "cuMemAlloc(wm)"); cu_ok(cuMemAlloc(&sub.ptr, sub_host.size()), "cuMemAlloc(sub)");
  cu_ok(cuMemcpyHtoD(wm.ptr, wm_host.data(), wm_host.size()), "cuMemcpyHtoD(wm)"); cu_ok(cuMemcpyHtoD(sub.ptr, sub_host.data(), sub_host.size()), "cuMemcpyHtoD(sub)");

  struct GpuOverlayItemHost { const unsigned char* ptr; int w, h, x, y; float opacity; };
  std::vector<GpuOverlayItemHost> layer_items = {
    {reinterpret_cast<const unsigned char*>(wm.ptr), wm.w, wm.h, wm.x, wm.y, wm.opacity},
    {reinterpret_cast<const unsigned char*>(sub.ptr), sub.w, sub.h, sub.x, sub.y, sub.opacity}
  };
  CUdeviceptr gpu_layers_table{};
  cu_ok(cuMemAlloc(&gpu_layers_table, layer_items.size() * sizeof(GpuOverlayItemHost)), "cuMemAlloc(layer_items)");
  cu_ok(cuMemcpyHtoD(gpu_layers_table, layer_items.data(), layer_items.size() * sizeof(GpuOverlayItemHost)), "cuMemcpyHtoD(layer_items)");
  int layer_count = static_cast<int>(layer_items.size());

  CUdeviceptr src_y{}, src_uv{}, dst_y{}, dst_uv{};
  size_t pitch = (W + 31) & ~31;
  cu_ok(cuMemAlloc(&src_y, pitch * H), "alloc"); cu_ok(cuMemAlloc(&src_uv, pitch * H / 2), "alloc");
  cu_ok(cuMemAlloc(&dst_y, pitch * H), "alloc"); cu_ok(cuMemAlloc(&dst_uv, pitch * H / 2), "alloc");

  CUevent ev_start{}, ev_end{};
  cu_ok(cuEventCreate(&ev_start, CU_EVENT_DEFAULT), "create");
  cu_ok(cuEventCreate(&ev_end, CU_EVENT_DEFAULT), "create");

  int ipitch = (int)pitch;
  void* args[] = {&dst_y, &dst_uv, &ipitch, &ipitch, &src_y, &src_uv, &ipitch, &ipitch, &gpu_layers_table, &layer_count, &W, &H};

  auto wall_start = Clock::now();
  cu_ok(cuEventRecord(ev_start, stream), "record start");
  for (int i = 0; i < count; ++i) {
    cu_ok(cuLaunchKernel(kernel, (W + 31) / 32, (H + 31) / 32, 1, 32, 16, 1, 0, stream, args, nullptr), "launch");
  }
  cu_ok(cuEventRecord(ev_end, stream), "record end");
  cu_ok(cuEventSynchronize(ev_end), "sync");
  auto wall_end = Clock::now();

  float gpu_ms = 0.0f;
  cu_ok(cuEventElapsedTime(&gpu_ms, ev_start, ev_end), "elapsed");
  double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

  cuEventDestroy(ev_start); cuEventDestroy(ev_end);
  cuMemFree(src_y); cuMemFree(src_uv); cuMemFree(dst_y); cuMemFree(dst_uv);
  cuMemFree(wm.ptr); cuMemFree(sub.ptr); cuMemFree(gpu_layers_table);
  cuModuleUnload(mod); cuStreamDestroy(stream); cuDevicePrimaryCtxRelease(dev);

  std::cout << "[CEILING GPU-OVERLAY-ONLY] frames=" << count << " pure_gpu=" << gpu_ms << " ms (" << (1000.0 * count / gpu_ms) << " FPS) wall=" << wall_ms << " ms\n";
  return 0;
}

int run_decode_only(const char* input) {
  auto start = Clock::now();
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* in{}; av_ok(avformat_open_input(&in, input, nullptr, nullptr), "open");
  av_ok(avformat_find_stream_info(in, nullptr), "find_stream");
  int si = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const AVCodec* dec = avcodec_find_decoder_by_name("h264_cuvid");
  if (!dec) dec = avcodec_find_decoder(in->streams[si]->codecpar->codec_id);
  AVCodecContext* dc = avcodec_alloc_context3(dec);
  av_ok(avcodec_parameters_to_context(dc, in->streams[si]->codecpar), "params");
  dc->get_format = hw_format;
  dc->extra_hw_frames = 8;
  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  dc->hw_device_ctx = av_buffer_ref(hwdev);
  av_ok(avcodec_open2(dc, dec, nullptr), "open2");

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  uint64_t count = 0;
  while (av_read_frame(in, pkt) >= 0) {
    if (pkt->stream_index == si) {
      av_ok(avcodec_send_packet(dc, pkt), "send");
      while (avcodec_receive_frame(dc, frame) >= 0) count++;
    }
    av_packet_unref(pkt);
  }
  avcodec_send_packet(dc, nullptr);
  while (avcodec_receive_frame(dc, frame) >= 0) count++;

  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dc);
  avformat_close_input(&in);
  av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVDEC-ONLY] decoder=" << dec->name << " frames=" << count << " total=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_gpu_only(int count, int W, int H) {
  auto start = Clock::now();
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
  CUstream stream{}; cu_ok(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "stream");

  nvrtcProgram prog{}; nv_ok(nvrtcCreateProgram(&prog, kKernel, "composite.cu", 0, nullptr, nullptr), "createProg");
  // Canonical target for this stack is Ampere (compute_86, RTX A4000).
  const char* opts[] = {"--gpu-architecture=compute_86"};
  nv_ok(nvrtcCompileProgram(prog, 1, opts), "compileProg");
  size_t ptx_size{}; nv_ok(nvrtcGetPTXSize(prog, &ptx_size), "ptxSize");
  std::vector<char> ptx(ptx_size); nv_ok(nvrtcGetPTX(prog, ptx.data()), "getPTX"); nvrtcDestroyProgram(&prog);
  CUmodule mod{}; CUfunction kernel{}; cu_ok(cuModuleLoadData(&mod, ptx.data()), "loadData");
  cu_ok(cuModuleGetFunction(&kernel, mod, "fused_nv12_composite"), "getFunc");

  CUdeviceptr src_y{}, src_uv{}, dst_y{}, dst_uv{};
  size_t pitch = (W + 31) & ~31;
  cu_ok(cuMemAlloc(&src_y, pitch * H), "alloc"); cu_ok(cuMemAlloc(&src_uv, pitch * H / 2), "alloc");
  cu_ok(cuMemAlloc(&dst_y, pitch * H), "alloc"); cu_ok(cuMemAlloc(&dst_uv, pitch * H / 2), "alloc");

  CUevent ev_start{}, ev_end{};
  cu_ok(cuEventCreate(&ev_start, CU_EVENT_DEFAULT), "create");
  cu_ok(cuEventCreate(&ev_end, CU_EVENT_DEFAULT), "create");

  CUdeviceptr dummy_table{0}; int layer_count = 0; int ipitch = (int)pitch;
  void* args[] = {&dst_y, &dst_uv, &ipitch, &ipitch, &src_y, &src_uv, &ipitch, &ipitch, &dummy_table, &layer_count, &W, &H};

  cu_ok(cuEventRecord(ev_start, stream), "record start");
  for (int i = 0; i < count; ++i) {
    cu_ok(cuLaunchKernel(kernel, (W + 31) / 32, (H + 31) / 32, 1, 32, 16, 1, 0, stream, args, nullptr), "launch");
  }
  cu_ok(cuEventRecord(ev_end, stream), "record end");
  cu_ok(cuEventSynchronize(ev_end), "sync");

  float gpu_ms = 0.0f;
  cu_ok(cuEventElapsedTime(&gpu_ms, ev_start, ev_end), "elapsed");
  auto end = Clock::now();
  double wall_ms = std::chrono::duration<double, std::milli>(end - start).count();

  cuEventDestroy(ev_start); cuEventDestroy(ev_end);
  cuMemFree(src_y); cuMemFree(src_uv); cuMemFree(dst_y); cuMemFree(dst_uv);
  cuModuleUnload(mod); cuStreamDestroy(stream); cuDevicePrimaryCtxRelease(dev);

  std::cout << "[CEILING GPU-ONLY] frames=" << count << " pure_gpu=" << gpu_ms << " ms (" << (1000.0 * count / gpu_ms) << " FPS) wall=" << wall_ms << " ms\n";
  return 0;
}

} // namespace
