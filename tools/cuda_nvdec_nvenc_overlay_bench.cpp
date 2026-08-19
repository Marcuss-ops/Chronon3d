// Native CUDA video benchmark: NVDEC -> CUDA NV12 composite -> NVENC.
// Overlay layers are small RGBA device buffers uploaded once.  The video
// frame never enters host memory after decode.
#include <cuda.h>
#include <nvrtc.h>

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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

namespace {

constexpr const char* kKernel = R"CUDA(
extern "C" __global__ void composite_nv12(
    unsigned char* yout, unsigned char* uvout, int yp, int uvp,
    const unsigned char* wm, int ww, int wh, int wx, int wy, float wopacity,
    const unsigned char* sub, int sw, int sh, int sx, int sy,
    int width, int height) {
  const int bx = (int)(blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int by = (int)(blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (bx >= width || by >= height) return;
  const int x1 = min(bx + 1, width - 1);
  const int y1 = min(by + 1, height - 1);
  const int uvx = bx & ~1;
  const int uvy = by >> 1;
  const unsigned char base_uv_u = uvout[uvy * uvp + uvx];
  const unsigned char base_uv_v = uvout[uvy * uvp + uvx + 1];
  const float bu = ((float)base_uv_u - 128.0f) / 224.0f;
  const float bv = ((float)base_uv_v - 128.0f) / 224.0f;
  float rgb[4][3];
  int px[4] = {bx, x1, bx, x1};
  int py[4] = {by, by, y1, y1};
  for (int i = 0; i < 4; ++i) {
    const float yy = ((float)yout[py[i] * yp + px[i]] - 16.0f) / 219.0f;
    float r = yy + 1.5748f * bv;
    float g = yy - 0.1873f * bu - 0.4681f * bv;
    float b = yy + 1.8556f * bu;
    const int layers[2] = {0, 1};
    for (int li = 0; li < 2; ++li) {
      const unsigned char* layer = li == 0 ? wm : sub;
      const int lw = li == 0 ? ww : sw, lh = li == 0 ? wh : sh;
      const int ox = li == 0 ? wx : sx, oy = li == 0 ? wy : sy;
      const float op = li == 0 ? wopacity : 1.0f;
      const int lx = px[i] - ox, ly = py[i] - oy;
      if (!layer || lx < 0 || ly < 0 || lx >= lw || ly >= lh) continue;
      const unsigned char* p = layer + ((long long)ly * lw + lx) * 4;
      const float a = ((float)p[3] / 255.0f) * op;
      r = (float)p[0] / 255.0f * a + r * (1.0f - a);
      g = (float)p[1] / 255.0f * a + g * (1.0f - a);
      b = (float)p[2] / 255.0f * a + b * (1.0f - a);
    }
    r = fminf(fmaxf(r, 0.0f), 1.0f);
    g = fminf(fmaxf(g, 0.0f), 1.0f);
    b = fminf(fmaxf(b, 0.0f), 1.0f);
    rgb[i][0] = r; rgb[i][1] = g; rgb[i][2] = b;
    yout[py[i] * yp + px[i]] = (unsigned char)fminf(fmaxf(16.0f + 219.0f *
        (0.2126f * r + 0.7152f * g + 0.0722f * b), 0.0f), 255.0f);
  }
  const float ar = (rgb[0][0] + rgb[1][0] + rgb[2][0] + rgb[3][0]) * 0.25f;
  const float ag = (rgb[0][1] + rgb[1][1] + rgb[2][1] + rgb[3][1]) * 0.25f;
  const float ab = (rgb[0][2] + rgb[1][2] + rgb[2][2] + rgb[3][2]) * 0.25f;
  const float u = -0.1146f * ar - 0.3854f * ag + 0.5000f * ab;
  const float v =  0.5000f * ar - 0.4542f * ag - 0.0458f * ab;
  uvout[uvy * uvp + uvx] = (unsigned char)fminf(fmaxf(128.0f + 224.0f * u, 0.0f), 255.0f);
  uvout[uvy * uvp + uvx + 1] = (unsigned char)fminf(fmaxf(128.0f + 224.0f * v, 0.0f), 255.0f);
}
)CUDA";

struct Layer { CUdeviceptr ptr{}; int w{}, h{}, x{}, y{}; };

[[noreturn]] void fail(const std::string& s) { throw std::runtime_error(s); }
void cu_ok(CUresult r, const char* what) { if (r != CUDA_SUCCESS) fail(std::string(what) + " CUDA error " + std::to_string((int)r)); }
void av_ok(int r, const char* what) { if (r < 0) { char b[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(r,b,sizeof(b)); fail(std::string(what)+": "+b); } }
void nv_ok(nvrtcResult r, const char* what) { if (r != NVRTC_SUCCESS) fail(std::string(what)+": "+nvrtcGetErrorString(r)); }

std::vector<unsigned char> read_rgba(const char* path, int w, int h) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(std::string("cannot open layer ") + path);
  std::vector<unsigned char> data((size_t)w * h * 4);
  f.read(reinterpret_cast<char*>(data.data()), (std::streamsize)data.size());
  if (f.gcount() != (std::streamsize)data.size()) fail(std::string("short layer ") + path);
  return data;
}

enum AVPixelFormat hw_format(AVCodecContext*, const enum AVPixelFormat* formats) {
  for (const auto* p = formats; *p != AV_PIX_FMT_NONE; ++p) if (*p == AV_PIX_FMT_CUDA) return *p;
  return AV_PIX_FMT_NONE;
}

void copy_plane(CUdeviceptr dst, int dp, const uint8_t* src, int sp, int w, int h, CUstream stream) {
  CUDA_MEMCPY2D c{}; c.srcMemoryType = CU_MEMORYTYPE_DEVICE; c.srcDevice = (CUdeviceptr)src;
  c.srcPitch = sp; c.dstMemoryType = CU_MEMORYTYPE_DEVICE; c.dstDevice = dst;
  c.dstPitch = dp; c.WidthInBytes = w; c.Height = h; cu_ok(cuMemcpy2DAsync(&c, stream), "cuMemcpy2DAsync");
}

void write_packets(AVCodecContext* enc, AVFormatContext* out, AVPacket* pkt, AVFrame* frame, int64_t& count) {
  av_ok(avcodec_send_frame(enc, frame), "avcodec_send_frame");
  for (;;) {
    int r = avcodec_receive_packet(enc, pkt);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
    av_ok(r, "avcodec_receive_packet");
    pkt->stream_index = 0;
    pkt->pts = pkt->dts = count++;
    pkt->duration = 1;
    av_packet_rescale_ts(pkt, enc->time_base, out->streams[0]->time_base);
    av_ok(av_interleaved_write_frame(out, pkt), "av_interleaved_write_frame");
    av_packet_unref(pkt);
  }
}

} // namespace

int main(int argc, char** argv) {
  av_log_set_level(AV_LOG_ERROR);
  if (argc != 13) {
    std::fprintf(stderr, "usage: %s input.mp4 output.mp4 wm.rgba wm_w wm_h wm_x wm_y sub.rgba sub_w sub_h sub_x sub_y\n", argv[0]);
    return 2;
  }
  try {
    const char* input = argv[1]; const char* output = argv[2];
    const int ww = std::atoi(argv[4]), wh = std::atoi(argv[5]), wx = std::atoi(argv[6]), wy = std::atoi(argv[7]);
    const int sw = std::atoi(argv[9]), sh = std::atoi(argv[10]), sx = std::atoi(argv[11]), sy = std::atoi(argv[12]);
    CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev,0), "cuDeviceGet");
    cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
    cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
    cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
    CUstream stream{}; cu_ok(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    const auto wm_host = read_rgba(argv[3], ww, wh), sub_host = read_rgba(argv[8], sw, sh);
    Layer wm{.w=ww,.h=wh,.x=wx,.y=wy}, sub{.w=sw,.h=sh,.x=sx,.y=sy};
    cu_ok(cuMemAlloc(&wm.ptr, wm_host.size()), "cuMemAlloc(wm)"); cu_ok(cuMemAlloc(&sub.ptr, sub_host.size()), "cuMemAlloc(sub)");
    cu_ok(cuMemcpyHtoD(wm.ptr, wm_host.data(), wm_host.size()), "cuMemcpyHtoD(wm)"); cu_ok(cuMemcpyHtoD(sub.ptr, sub_host.data(), sub_host.size()), "cuMemcpyHtoD(sub)");
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
    CUmodule mod{}; CUfunction kernel{}; cu_ok(cuModuleLoadData(&mod,ptx.data()),"cuModuleLoadData"); cu_ok(cuModuleGetFunction(&kernel,mod,"composite_nv12"),"cuModuleGetFunction");

    AVFormatContext* in{}; av_ok(avformat_open_input(&in,input,nullptr,nullptr),"avformat_open_input"); av_ok(avformat_find_stream_info(in,nullptr),"avformat_find_stream_info");
    const int si = av_find_best_stream(in,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0); if(si<0) fail("video stream missing");
    const char* decoder_name = std::getenv("CHRONON_CUDA_DECODER");
    const AVCodec* dec = (decoder_name && *decoder_name) ? avcodec_find_decoder_by_name(decoder_name) : avcodec_find_decoder(in->streams[si]->codecpar->codec_id);
    if(!dec) fail(std::string("decoder missing: ") + (decoder_name && *decoder_name ? decoder_name : "default"));
    AVCodecContext* dc = avcodec_alloc_context3(dec); if(!dc) fail("decoder alloc"); av_ok(avcodec_parameters_to_context(dc,in->streams[si]->codecpar),"decoder parameters"); dc->thread_count = 1; dc->thread_type = 0;
    dc->get_format = hw_format; AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev,AV_HWDEVICE_TYPE_CUDA,"0",nullptr,AV_CUDA_USE_PRIMARY_CONTEXT),"av_hwdevice_ctx_create"); dc->hw_device_ctx=av_buffer_ref(hwdev); av_ok(avcodec_open2(dc,dec,nullptr),"avcodec_open2");
    const int W=dc->width, H=dc->height; if(W<=0||H<=0) fail("invalid dimensions");
    AVFormatContext* out{}; avformat_alloc_output_context2(&out,nullptr,nullptr,output); if(!out) fail("output alloc");
    const AVCodec* enc_codec=avcodec_find_encoder_by_name("h264_nvenc"); if(!enc_codec) fail("h264_nvenc missing"); AVStream* st=avformat_new_stream(out,enc_codec); if(!st) fail("stream alloc");
    const int nvenc_surfaces = std::max(1, std::atoi(std::getenv("CHRONON_NVENC_SURFACES") ?: "2"));
    AVCodecContext* ec=avcodec_alloc_context3(enc_codec); if(!ec) fail("encoder alloc"); ec->width=W; ec->height=H; ec->time_base={1,24}; ec->framerate={24,1}; ec->pix_fmt=AV_PIX_FMT_CUDA; ec->bit_rate=0; ec->thread_count = 1; ec->thread_type = 0; av_opt_set(ec->priv_data,"preset","p1",0); av_opt_set_int(ec->priv_data,"qp",23,0); av_opt_set_int(ec->priv_data,"surfaces",nvenc_surfaces,0); av_opt_set_int(ec->priv_data,"zerolatency",1,0);
    AVBufferRef* frames=av_hwframe_ctx_alloc(hwdev); if(!frames) fail("frames alloc"); auto* fctx=(AVHWFramesContext*)frames->data; fctx->format=AV_PIX_FMT_CUDA; fctx->sw_format=AV_PIX_FMT_NV12; fctx->width=W; fctx->height=H; fctx->initial_pool_size=8; av_ok(av_hwframe_ctx_init(frames),"av_hwframe_ctx_init"); ec->hw_frames_ctx=av_buffer_ref(frames); av_ok(avcodec_open2(ec,enc_codec,nullptr),"encoder open"); av_ok(avcodec_parameters_from_context(st->codecpar,ec),"codec parameters"); st->time_base=ec->time_base;
    if(!(out->oformat->flags&AVFMT_NOFILE)) av_ok(avio_open(&out->pb,output,AVIO_FLAG_WRITE),"avio_open"); av_ok(avformat_write_header(out,nullptr),"avformat_write_header");
    AVPacket* pkt=av_packet_alloc(); AVFrame* decoded=av_frame_alloc(); if(!pkt || !decoded) fail("frame/packet alloc"); int64_t out_count=0;
    const int pacing_us = std::max(0, std::atoi(std::getenv("CHRONON_FRAME_PACING_US") ?: "0"));
    auto process=[&](AVFrame* src){
      AVFrame* dst=av_frame_alloc(); if(!dst) fail("frame alloc"); dst->format=AV_PIX_FMT_CUDA; dst->width=W; dst->height=H; dst->hw_frames_ctx=av_buffer_ref(frames); av_ok(av_hwframe_get_buffer(frames,dst,0),"av_hwframe_get_buffer");
      copy_plane((CUdeviceptr)dst->data[0],dst->linesize[0],src->data[0],src->linesize[0],W,H,stream); copy_plane((CUdeviceptr)dst->data[1],dst->linesize[1],src->data[1],src->linesize[1],W,(H+1)/2,stream);
      float wop = 0.75f; int kernel_w = W, kernel_h = H;
      void* args[]={&dst->data[0],&dst->data[1],&dst->linesize[0],&dst->linesize[1],&wm.ptr,&wm.w,&wm.h,&wm.x,&wm.y,&wop,&sub.ptr,&sub.w,&sub.h,&sub.x,&sub.y,&kernel_w,&kernel_h};
      cu_ok(cuLaunchKernel(kernel,(W+31)/32,(H+31)/32,1,32,16,1,0,stream,args,nullptr),"cuLaunchKernel"); cu_ok(cuStreamSynchronize(stream),"cuStreamSynchronize"); dst->pts=out_count; write_packets(ec,out,pkt,dst,out_count); av_frame_free(&dst); if (pacing_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(pacing_us));
    };
    while(av_read_frame(in,pkt)>=0){ if(pkt->stream_index==si){ av_ok(avcodec_send_packet(dc,pkt),"avcodec_send_packet"); while(avcodec_receive_frame(dc,decoded)>=0) process(decoded); } av_packet_unref(pkt); }
    avcodec_send_packet(dc,nullptr); while(avcodec_receive_frame(dc,decoded)>=0) process(decoded); write_packets(ec,out,pkt,nullptr,out_count); av_write_trailer(out);
    if(!(out->oformat->flags&AVFMT_NOFILE)) avio_closep(&out->pb); av_frame_free(&decoded); av_packet_free(&pkt); avcodec_free_context(&ec); avcodec_free_context(&dc); avformat_close_input(&in); avformat_free_context(out); av_buffer_unref(&frames); av_buffer_unref(&hwdev); cuMemFree(wm.ptr); cuMemFree(sub.ptr); cuModuleUnload(mod); cuStreamDestroy(stream); cuDevicePrimaryCtxRelease(dev); std::cout << "CUDA_NVDEC_NVENC_OVERLAY_PASS decoder=" << dec->name << " surfaces=" << nvenc_surfaces << " output=" << output << " frames=" << out_count << "\n"; return 0;
  } catch(const std::exception& e){ std::cerr << "CUDA_NVDEC_NVENC_OVERLAY_FAIL: " << e.what() << "\n"; return 1; }
}
