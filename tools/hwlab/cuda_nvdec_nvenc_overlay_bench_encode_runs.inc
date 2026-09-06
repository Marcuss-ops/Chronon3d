namespace {

int run_encode_only_warm(int count, int W, int H, const char* output) {
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* out{}; avformat_alloc_output_context2(&out, nullptr, nullptr, output);
  const AVCodec* enc_codec = avcodec_find_encoder_by_name("h264_nvenc");
  AVStream* st = avformat_new_stream(out, enc_codec);
  AVCodecContext* ec = avcodec_alloc_context3(enc_codec);
  ec->width = W; ec->height = H; ec->time_base = {1, 24}; ec->framerate = {24, 1}; ec->pix_fmt = AV_PIX_FMT_CUDA;
  ec->bit_rate = 0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  av_opt_set(ec->priv_data, "preset", "p1", 0); av_opt_set_int(ec->priv_data, "qp", 23, 0);
  av_opt_set_int(ec->priv_data, "surfaces", 8, 0); av_opt_set_int(ec->priv_data, "zerolatency", 1, 0);

  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  AVBufferRef* frames = av_hwframe_ctx_alloc(hwdev);
  auto* fctx = (AVHWFramesContext*)frames->data;
  fctx->format = AV_PIX_FMT_CUDA; fctx->sw_format = AV_PIX_FMT_NV12; fctx->width = W; fctx->height = H; fctx->initial_pool_size = 16;
  av_ok(av_hwframe_ctx_init(frames), "init");
  ec->hw_frames_ctx = av_buffer_ref(frames);
  av_ok(avcodec_open2(ec, enc_codec, nullptr), "open");
  av_ok(avcodec_parameters_from_context(st->codecpar, ec), "params");
  st->time_base = ec->time_base;

  if (!(out->oformat->flags & AVFMT_NOFILE)) av_ok(avio_open(&out->pb, output, AVIO_FLAG_WRITE), "avio");
  av_ok(avformat_write_header(out, nullptr), "header");

  std::vector<AVFrame*> pool(16, nullptr);
  for (auto& f : pool) {
    f = av_frame_alloc();
    f->format = AV_PIX_FMT_CUDA; f->width = W; f->height = H; f->hw_frames_ctx = av_buffer_ref(frames);
    av_ok(av_hwframe_get_buffer(frames, f, 0), "get_buffer");
  }

  AVPacket* pkt = av_packet_alloc();
  uint64_t encoded_packet_count = 0;

  // WARM SYNC ENCODE MEASUREMENT ONLY
  auto start = Clock::now();
  for (int i = 0; i < count; ++i) {
    AVFrame* f = pool[i % pool.size()];
    f->pts = i;
    write_packets(ec, out, pkt, f, encoded_packet_count);
  }
  write_packets(ec, out, pkt, nullptr, encoded_packet_count);
  av_write_trailer(out);
  auto end = Clock::now();

  if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
  for (auto& f : pool) av_frame_free(&f);
  av_packet_free(&pkt); avcodec_free_context(&ec); avformat_free_context(out);
  av_buffer_unref(&frames); av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVENC-ONLY-WARM-SYNC] frames=" << count << " warm_throughput=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_encode_only_async(int count, int W, int H, const char* output) {
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* out{}; avformat_alloc_output_context2(&out, nullptr, nullptr, output);
  const AVCodec* enc_codec = avcodec_find_encoder_by_name("h264_nvenc");
  AVStream* st = avformat_new_stream(out, enc_codec);
  AVCodecContext* ec = avcodec_alloc_context3(enc_codec);
  ec->width = W; ec->height = H; ec->time_base = {1, 24}; ec->framerate = {24, 1}; ec->pix_fmt = AV_PIX_FMT_CUDA;
  ec->bit_rate = 0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  av_opt_set(ec->priv_data, "preset", "p1", 0); av_opt_set_int(ec->priv_data, "qp", 23, 0);
  av_opt_set_int(ec->priv_data, "surfaces", 8, 0); av_opt_set_int(ec->priv_data, "zerolatency", 1, 0);

  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  AVBufferRef* frames = av_hwframe_ctx_alloc(hwdev);
  auto* fctx = (AVHWFramesContext*)frames->data;
  fctx->format = AV_PIX_FMT_CUDA; fctx->sw_format = AV_PIX_FMT_NV12; fctx->width = W; fctx->height = H; fctx->initial_pool_size = 16;
  av_ok(av_hwframe_ctx_init(frames), "init");
  ec->hw_frames_ctx = av_buffer_ref(frames);
  av_ok(avcodec_open2(ec, enc_codec, nullptr), "open");
  av_ok(avcodec_parameters_from_context(st->codecpar, ec), "params");
  st->time_base = ec->time_base;

  if (!(out->oformat->flags & AVFMT_NOFILE)) av_ok(avio_open(&out->pb, output, AVIO_FLAG_WRITE), "avio");
  av_ok(avformat_write_header(out, nullptr), "header");

  const size_t kRingSlots = 16;
  FrameSlotRing slot_ring(kRingSlots, frames, W, H);

  struct AsyncItem { FrameSlot* slot{nullptr}; uint64_t frame_id{0}; bool is_flush{false}; };
  std::queue<AsyncItem> q;
  std::mutex q_mtx;
  std::condition_variable q_cv;
  uint64_t encoded_packet_count = 0;

  std::thread drain_thread([&]() {
    AVPacket* local_pkt = av_packet_alloc();
    while (true) {
      AsyncItem item{};
      {
        std::unique_lock<std::mutex> lock(q_mtx);
        q_cv.wait(lock, [&]() { return !q.empty(); });
        item = q.front();
        q.pop();
      }
      if (item.is_flush) {
        write_packets(ec, out, local_pkt, nullptr, encoded_packet_count);
        break;
      }
      item.slot->frame->pts = static_cast<int64_t>(item.frame_id);
      write_packets(ec, out, local_pkt, item.slot->frame, encoded_packet_count);
      slot_ring.release_slot(item.slot);
    }
    av_packet_free(&local_pkt);
  });

  // WARM ASYNC ENCODE MEASUREMENT ONLY
  auto start = Clock::now();
  for (int i = 0; i < count; ++i) {
    FrameSlot* slot = slot_ring.acquire_free_slot();
    slot_ring.mark_ready(slot);
    {
      std::lock_guard<std::mutex> lock(q_mtx);
      q.push({slot, static_cast<uint64_t>(i), false});
    }
    q_cv.notify_one();
  }

  {
    std::lock_guard<std::mutex> lock(q_mtx);
    q.push({nullptr, 0, true});
  }
  q_cv.notify_one();
  if (drain_thread.joinable()) drain_thread.join();
  av_write_trailer(out);
  auto end = Clock::now();

  if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
  avcodec_free_context(&ec); avformat_free_context(out);
  av_buffer_unref(&frames); av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVENC-ONLY-WARM-ASYNC] frames=" << count << " warm_throughput=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_encode_only(int count, int W, int H, const char* output) {
  auto start = Clock::now();
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* out{}; avformat_alloc_output_context2(&out, nullptr, nullptr, output);
  const AVCodec* enc_codec = avcodec_find_encoder_by_name("h264_nvenc");
  AVStream* st = avformat_new_stream(out, enc_codec);
  AVCodecContext* ec = avcodec_alloc_context3(enc_codec);
  ec->width = W; ec->height = H; ec->time_base = {1, 24}; ec->framerate = {24, 1}; ec->pix_fmt = AV_PIX_FMT_CUDA;
  ec->bit_rate = 0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  av_opt_set(ec->priv_data, "preset", "p1", 0); av_opt_set_int(ec->priv_data, "qp", 23, 0);
  av_opt_set_int(ec->priv_data, "surfaces", 8, 0); av_opt_set_int(ec->priv_data, "zerolatency", 1, 0);

  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  AVBufferRef* frames = av_hwframe_ctx_alloc(hwdev);
  auto* fctx = (AVHWFramesContext*)frames->data;
  fctx->format = AV_PIX_FMT_CUDA; fctx->sw_format = AV_PIX_FMT_NV12; fctx->width = W; fctx->height = H; fctx->initial_pool_size = 8;
  av_ok(av_hwframe_ctx_init(frames), "init");
  ec->hw_frames_ctx = av_buffer_ref(frames);
  av_ok(avcodec_open2(ec, enc_codec, nullptr), "open");
  av_ok(avcodec_parameters_from_context(st->codecpar, ec), "params");
  st->time_base = ec->time_base;

  if (!(out->oformat->flags & AVFMT_NOFILE)) av_ok(avio_open(&out->pb, output, AVIO_FLAG_WRITE), "avio");
  av_ok(avformat_write_header(out, nullptr), "header");

  AVFrame* frame = av_frame_alloc();
  frame->format = AV_PIX_FMT_CUDA; frame->width = W; frame->height = H; frame->hw_frames_ctx = av_buffer_ref(frames);
  av_ok(av_hwframe_get_buffer(frames, frame, 0), "get_buffer");

  AVPacket* pkt = av_packet_alloc();
  uint64_t encoded_packet_count = 0;
  for (int i = 0; i < count; ++i) {
    frame->pts = i;
    write_packets(ec, out, pkt, frame, encoded_packet_count);
  }
  write_packets(ec, out, pkt, nullptr, encoded_packet_count);
  av_write_trailer(out);

  if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
  av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&ec); avformat_free_context(out);
  av_buffer_unref(&frames); av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVENC-ONLY] frames=" << count << " total=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

} // namespace
