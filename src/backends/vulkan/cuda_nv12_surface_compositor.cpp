#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#ifndef CHRONON3D_NVRTC_ARCHITECTURE
#define CHRONON3D_NVRTC_ARCHITECTURE "compute_75"
#endif

#include <nvrtc.h>

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>
#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <system_error>

namespace chronon3d::backends::vulkan {
namespace {

struct alignas(8) DirectYuvLayerHost {
    CUdeviceptr rgba{0};
    int pitch{0};
    int source_width{0};
    int source_height{0};
    float dst_x0{0.0f}, dst_y0{0.0f}, dst_x1{0.0f}, dst_y1{0.0f};
    float src_x0{0.0f}, src_y0{0.0f}, src_x1{1.0f}, src_y1{1.0f};
    float opacity{1.0f};
    int blend_mode{0};
};
static_assert(sizeof(DirectYuvLayerHost) == 64);

constexpr const char* kNv12Kernel = R"CUDA(
extern "C" __global__ void nv12_to_rgba(
    const unsigned char* y, int yp, const unsigned char* uv, int uvp,
    cudaSurfaceObject_t out, int width, int height,
    float y_offset, float y_scale, float uv_offset, float uv_scale,
    float r_v, float g_u, float g_v, float b_u) {
  const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  const int yy = (int)(blockIdx.y * blockDim.y + threadIdx.y);
  if (x >= width || yy >= height) return;
  const float yf = ((float)y[yy * yp + x] - y_offset) * y_scale;
  const int uvx = x & ~1;
  const float u = ((float)uv[(yy >> 1) * uvp + uvx] - uv_offset) * uv_scale;
  const float v = ((float)uv[(yy >> 1) * uvp + uvx + 1] - uv_offset) * uv_scale;
  float r = yf + r_v * v;
  float g = yf + g_u * u + g_v * v;
  float b = yf + b_u * u;
  r = fminf(fmaxf(r, 0.0f), 1.0f);
  g = fminf(fmaxf(g, 0.0f), 1.0f);
  b = fminf(fmaxf(b, 0.0f), 1.0f);
  surf2Dwrite(make_float4(r, g, b, 1.0f), out, x * (int)sizeof(float4), yy);
}

extern "C" __global__ void p010_to_rgba(
    const unsigned short* y, int yp, const unsigned short* uv, int uvp,
    cudaSurfaceObject_t out, int width, int height,
    float y_offset, float y_scale, float uv_offset, float uv_scale,
    float r_v, float g_u, float g_v, float b_u) {
  const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  const int yy = (int)(blockIdx.y * blockDim.y + threadIdx.y);
  if (x >= width || yy >= height) return;

  // P010 stores the 10-bit sample left-shifted by six in each 16-bit word.
  // The luma/chroma code values use the same nominal video-range endpoints
  // as 8-bit YUV, scaled by four: Y 64..940 and UV 64..960.
  const float yf = ((float)(y[yy * (yp / 2) + x] >> 6) - y_offset) * y_scale;
  const int uvx = x & ~1;
  const int uv_row = (yy >> 1) * (uvp / 2);
  const float u = ((float)(uv[uv_row + uvx] >> 6) - uv_offset) * uv_scale;
  const float v = ((float)(uv[uv_row + uvx + 1] >> 6) - uv_offset) * uv_scale;
  float r = yf + r_v * v;
  float g = yf + g_u * u + g_v * v;
  float b = yf + b_u * u;
  r = fminf(fmaxf(r, 0.0f), 1.0f);
  g = fminf(fmaxf(g, 0.0f), 1.0f);
  b = fminf(fmaxf(b, 0.0f), 1.0f);
  surf2Dwrite(make_float4(r, g, b, 1.0f), out, x * (int)sizeof(float4), yy);
}

extern "C" __global__ void nv12_composite_overlay(
    const unsigned char* bg_y, int bg_yp, const unsigned char* bg_uv, int bg_uvp,
    cudaSurfaceObject_t fg_rgba,
    unsigned char* out_y, int out_yp, unsigned char* out_uv, int out_uvp,
    int width, int height) {
  const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  const int y = (int)(blockIdx.y * blockDim.y + threadIdx.y);
  if (x >= width || y >= height) return;

  float4 fg;
  surf2Dread(&fg, fg_rgba, x * (int)sizeof(float4), y);
  const float a = fminf(fmaxf(fg.w, 0.0f), 1.0f);

  const float bg_y_val = (float)bg_y[y * bg_yp + x];
  if (a <= 0.0f) {
    out_y[y * out_yp + x] = (unsigned char)bg_y_val;
  } else {
    const float y_fg = 16.0f + 219.0f * (0.2126f * fg.x + 0.7152f * fg.y + 0.0722f * fg.z);
    const float mixed_y = (1.0f - a) * bg_y_val + a * y_fg;
    out_y[y * out_yp + x] = (unsigned char)fminf(fmaxf(roundf(mixed_y), 16.0f), 235.0f);
  }

  if ((x & 1) == 0 && (y & 1) == 0 && (x + 1 < width) && (y + 1 < height)) {
    float4 fg10, fg01, fg11;
    surf2Dread(&fg10, fg_rgba, (x + 1) * (int)sizeof(float4), y);
    surf2Dread(&fg01, fg_rgba, x * (int)sizeof(float4), y + 1);
    surf2Dread(&fg11, fg_rgba, (x + 1) * (int)sizeof(float4), y + 1);

    const float a10 = fminf(fmaxf(fg10.w, 0.0f), 1.0f);
    const float a01 = fminf(fmaxf(fg01.w, 0.0f), 1.0f);
    const float a11 = fminf(fmaxf(fg11.w, 0.0f), 1.0f);

    const float a_sum = a + a10 + a01 + a11;
    const float a_block = a_sum * 0.25f;

    const int uv_idx = (y >> 1) * out_uvp + x;
    const int bg_uv_idx = (y >> 1) * bg_uvp + x;

    if (a_block <= 0.0f) {
      out_uv[uv_idx] = bg_uv[bg_uv_idx];
      out_uv[uv_idx + 1] = bg_uv[bg_uv_idx + 1];
    } else {
      const float r_avg = (fg.x * a + fg10.x * a10 + fg01.x * a01 + fg11.x * a11) / a_sum;
      const float g_avg = (fg.y * a + fg10.y * a10 + fg01.y * a01 + fg11.y * a11) / a_sum;
      const float b_avg = (fg.z * a + fg10.z * a10 + fg01.z * a01 + fg11.z * a11) / a_sum;

      const float u_fg = 128.0f + 224.0f * (-0.1146f * r_avg - 0.3854f * g_avg + 0.5000f * b_avg);
      const float v_fg = 128.0f + 224.0f * ( 0.5000f * r_avg - 0.4542f * g_avg - 0.0458f * b_avg);

      const float bg_u = (float)bg_uv[bg_uv_idx];
      const float bg_v = (float)bg_uv[bg_uv_idx + 1];

      const float u_out = (1.0f - a_block) * bg_u + a_block * u_fg;
      const float v_out = (1.0f - a_block) * bg_v + a_block * v_fg;

      out_uv[uv_idx] = (unsigned char)fminf(fmaxf(roundf(u_out), 16.0f), 240.0f);
      out_uv[uv_idx + 1] = (unsigned char)fminf(fmaxf(roundf(v_out), 16.0f), 240.0f);
    }
  }
}

__device__ float4 overlay_load(cudaSurfaceObject_t surface, int x, int y,
                               int width, int height) {
  if (x < 0 || y < 0 || x >= width || y >= height) return make_float4(0, 0, 0, 0);
  float4 value;
  surf2Dread(&value, surface, x * (int)sizeof(float4), y);
  return value;
}

__device__ void blend_luma(const unsigned char* bg_y, int bg_yp,
                           unsigned char* out_y, int out_yp,
                           int x, int y, float4 fg) {
  const float a = fminf(fmaxf(fg.w, 0.0f), 1.0f);
  const float bg = (float)bg_y[y * bg_yp + x];
  const float y_fg = 16.0f + 219.0f *
      (0.2126f * fg.x + 0.7152f * fg.y + 0.0722f * fg.z);
  const float value = (1.0f - a) * bg + a * y_fg;
  out_y[y * out_yp + x] = (unsigned char)fminf(fmaxf(roundf(value), 16.0f), 235.0f);
}

// One invocation owns one 2x2 luma block and its single interleaved UV pair.
// This is the native direct-YUV fast path: no RGBA overlay is allocated and
// the chroma decision is made from the same four source samples as luma.
extern "C" __global__ void nv12_composite_overlay_2x2(
    const unsigned char* bg_y, int bg_yp, const unsigned char* bg_uv, int bg_uvp,
    cudaSurfaceObject_t fg_rgba,
    unsigned char* out_y, int out_yp, unsigned char* out_uv, int out_uvp,
    int width, int height) {
  const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int y = (int)(blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (x >= width || y >= height) return;

  const float4 fg00 = overlay_load(fg_rgba, x, y, width, height);
  blend_luma(bg_y, bg_yp, out_y, out_yp, x, y, fg00);
  if (x + 1 < width) {
    blend_luma(bg_y, bg_yp, out_y, out_yp, x + 1, y,
               overlay_load(fg_rgba, x + 1, y, width, height));
  }
  if (y + 1 < height) {
    blend_luma(bg_y, bg_yp, out_y, out_yp, x, y + 1,
               overlay_load(fg_rgba, x, y + 1, width, height));
    if (x + 1 < width) {
      blend_luma(bg_y, bg_yp, out_y, out_yp, x + 1, y + 1,
                 overlay_load(fg_rgba, x + 1, y + 1, width, height));
    }
  }

  // NV12 still has one UV pair for the final odd row/column. Replicate the
  // edge sample instead of leaving that chroma pair stale.
  const int x1 = min(x + 1, width - 1);
  const int y1 = min(y + 1, height - 1);
  const float4 fg10 = overlay_load(fg_rgba, x1, y, width, height);
  const float4 fg01 = overlay_load(fg_rgba, x, y1, width, height);
  const float4 fg11 = overlay_load(fg_rgba, x1, y1, width, height);
  const float a0 = fminf(fmaxf(fg00.w, 0.0f), 1.0f);
  const float a1 = fminf(fmaxf(fg10.w, 0.0f), 1.0f);
  const float a2 = fminf(fmaxf(fg01.w, 0.0f), 1.0f);
  const float a3 = fminf(fmaxf(fg11.w, 0.0f), 1.0f);
  const float a_sum = a0 + a1 + a2 + a3;
  const float a_block = a_sum * 0.25f;
  const int uv_idx = (y >> 1) * out_uvp + x;
  const int bg_uv_idx = (y >> 1) * bg_uvp + x;
  if (a_block <= 0.0f) {
    out_uv[uv_idx] = bg_uv[bg_uv_idx];
    out_uv[uv_idx + 1] = bg_uv[bg_uv_idx + 1];
    return;
  }
  const float r = (fg00.x * a0 + fg10.x * a1 + fg01.x * a2 + fg11.x * a3) / a_sum;
  const float g = (fg00.y * a0 + fg10.y * a1 + fg01.y * a2 + fg11.y * a3) / a_sum;
  const float b = (fg00.z * a0 + fg10.z * a1 + fg01.z * a2 + fg11.z * a3) / a_sum;
  const float u_fg = 128.0f + 224.0f * (-0.1146f * r - 0.3854f * g + 0.5000f * b);
  const float v_fg = 128.0f + 224.0f * ( 0.5000f * r - 0.4542f * g - 0.0458f * b);
  out_uv[uv_idx] = (unsigned char)fminf(fmaxf(roundf((1.0f - a_block) * bg_uv[bg_uv_idx] + a_block * u_fg), 16.0f), 240.0f);
  out_uv[uv_idx + 1] = (unsigned char)fminf(fmaxf(roundf((1.0f - a_block) * bg_uv[bg_uv_idx + 1] + a_block * v_fg), 16.0f), 240.0f);
}

struct DirectYuvLayer {
  unsigned long long rgba;
  int pitch;
  int source_width;
  int source_height;
  float dst_x0, dst_y0, dst_x1, dst_y1;
  float src_x0, src_y0, src_x1, src_y1;
  float opacity;
  int blend_mode;
};

__device__ float4 direct_layer_load(const DirectYuvLayer& layer,
                                    int x, int y, int width, int height) {
  if (x < 0 || y < 0 || x >= width || y >= height || layer.rgba == 0 ||
      layer.source_width <= 0 || layer.source_height <= 0) {
    return make_float4(0, 0, 0, 0);
  }
  const float u = layer.src_x0 + (layer.src_x1 - layer.src_x0) *
      ((float)x - layer.dst_x0) / fmaxf(layer.dst_x1 - layer.dst_x0, 1.0f);
  const float v = layer.src_y0 + (layer.src_y1 - layer.src_y0) *
      ((float)y - layer.dst_y0) / fmaxf(layer.dst_y1 - layer.dst_y0, 1.0f);
  const int sx = min(max((int)floorf(u * (float)layer.source_width), 0), layer.source_width - 1);
  const int sy = min(max((int)floorf(v * (float)layer.source_height), 0), layer.source_height - 1);
  const char* row = (const char*)layer.rgba + sy * layer.pitch;
  return ((const float4*)row)[sx];
}

__device__ float4 direct_layer_pixel(const DirectYuvLayer* layers, int count,
                                     int x, int y, int width, int height) {
  float4 accum = make_float4(0, 0, 0, 0);
  for (int i = 0; i < count; ++i) {
    const DirectYuvLayer layer = layers[i];
    if (x < layer.dst_x0 || x >= layer.dst_x1 ||
        y < layer.dst_y0 || y >= layer.dst_y1) continue;
    float4 src = direct_layer_load(layer, x, y, width, height);
    src.w = fminf(fmaxf(src.w * layer.opacity, 0.0f), 1.0f);
    if (layer.blend_mode == 1) {
      accum.x += src.x * src.w;
      accum.y += src.y * src.w;
      accum.z += src.z * src.w;
      accum.w = fminf(accum.w + src.w, 1.0f);
    } else {
      accum.x = src.x * src.w + accum.x * (1.0f - src.w);
      accum.y = src.y * src.w + accum.y * (1.0f - src.w);
      accum.z = src.z * src.w + accum.z * (1.0f - src.w);
      accum.w = src.w + accum.w * (1.0f - src.w);
    }
  }
  return accum;
}

// Direct GpuLayerBatch -> NV12/P010 precursor. One invocation owns a 2x2
// block, so luma and chroma observe exactly the same ordered layer samples.
extern "C" __global__ void nv12_composite_layer_batch_2x2(
    const unsigned char* bg_y, int bg_yp, const unsigned char* bg_uv, int bg_uvp,
    unsigned char* out_y, int out_yp, unsigned char* out_uv, int out_uvp,
    const DirectYuvLayer* layers, int layer_count, int width, int height) {
  const int x = ((int)blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int y = ((int)blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (x >= width || y >= height) return;
  const int x1 = min(x + 1, width - 1);
  const int y1 = min(y + 1, height - 1);
  const float4 f00 = direct_layer_pixel(layers, layer_count, x, y, width, height);
  const float4 f10 = direct_layer_pixel(layers, layer_count, x1, y, width, height);
  const float4 f01 = direct_layer_pixel(layers, layer_count, x, y1, width, height);
  const float4 f11 = direct_layer_pixel(layers, layer_count, x1, y1, width, height);
  const float4 fs[4] = {f00, f10, f01, f11};
  const int xs[4] = {x, x1, x, x1};
  const int ys[4] = {y, y, y1, y1};
  for (int i = 0; i < 4; ++i) {
    const float y_fg = 16.0f + 219.0f *
        (0.2126f * fs[i].x + 0.7152f * fs[i].y + 0.0722f * fs[i].z);
    const float bg = (float)bg_y[ys[i] * bg_yp + xs[i]];
    out_y[ys[i] * out_yp + xs[i]] = (unsigned char)fminf(fmaxf(roundf(
        (1.0f - fs[i].w) * bg + fs[i].w * y_fg), 16.0f), 235.0f);
  }
  const float a = (f00.w + f10.w + f01.w + f11.w) * 0.25f;
  const int uv = (y >> 1) * out_uvp + x;
  const int bg_uv_index = (y >> 1) * bg_uvp + x;
  if (a <= 0.0f) {
    out_uv[uv] = bg_uv[bg_uv_index];
    out_uv[uv + 1] = bg_uv[bg_uv_index + 1];
    return;
  }
  const float r = (f00.x*f00.w + f10.x*f10.w + f01.x*f01.w + f11.x*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float g = (f00.y*f00.w + f10.y*f10.w + f01.y*f01.w + f11.y*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float b = (f00.z*f00.w + f10.z*f10.w + f01.z*f01.w + f11.z*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float u = 128.0f + 224.0f * (-0.1146f*r - 0.3854f*g + 0.5000f*b);
  const float v = 128.0f + 224.0f * ( 0.5000f*r - 0.4542f*g - 0.0458f*b);
  out_uv[uv] = (unsigned char)fminf(fmaxf(roundf((1.0f-a)*bg_uv[bg_uv_index] + a*u), 16.0f), 240.0f);
  out_uv[uv+1] = (unsigned char)fminf(fmaxf(roundf((1.0f-a)*bg_uv[bg_uv_index+1] + a*v), 16.0f), 240.0f);
}

__device__ inline float to_srgb_channel(float v) {
  v = fminf(fmaxf(v, 0.0f), 1.0f);
  return (v <= 0.0031308f) ? (v * 12.92f) : (1.055f * __powf(v, 1.0f / 2.4f) - 0.055f);
}

extern "C" __global__ void rgba_surface_to_nv12_2x2(
    cudaSurfaceObject_t fg_rgba,
    unsigned char* out_y, int out_yp, unsigned char* out_uv, int out_uvp,
    int width, int height) {
  const int x = ((int)blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int y = ((int)blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (x >= width || y >= height) return;
  const int x1 = min(x + 1, width - 1);
  const int y1 = min(y + 1, height - 1);
  const float4 f00 = overlay_load(fg_rgba, x, y, width, height);
  const float4 f10 = overlay_load(fg_rgba, x1, y, width, height);
  const float4 f01 = overlay_load(fg_rgba, x, y1, width, height);
  const float4 f11 = overlay_load(fg_rgba, x1, y1, width, height);
  const float4 fs[4] = {f00, f10, f01, f11};
  const int xs[4] = {x, x1, x, x1};
  const int ys[4] = {y, y, y1, y1};
  float3 srgb[4];
  for (int i = 0; i < 4; ++i) {
    const float a = fminf(fmaxf(fs[i].w, 0.0f), 1.0f);
    const float inv_a = (a > 1e-5f) ? (1.0f / a) : 0.0f;
    srgb[i].x = to_srgb_channel(fs[i].x * inv_a);
    srgb[i].y = to_srgb_channel(fs[i].y * inv_a);
    srgb[i].z = to_srgb_channel(fs[i].z * inv_a);
    const float y_value = 16.0f + 219.0f *
        (0.2126f * srgb[i].x + 0.7152f * srgb[i].y + 0.0722f * srgb[i].z);
    out_y[ys[i] * out_yp + xs[i]] = (unsigned char)fminf(
        fmaxf(roundf(y_value), 16.0f), 235.0f);
  }
  const float r = (srgb[0].x + srgb[1].x + srgb[2].x + srgb[3].x) * 0.25f;
  const float g = (srgb[0].y + srgb[1].y + srgb[2].y + srgb[3].y) * 0.25f;
  const float b = (srgb[0].z + srgb[1].z + srgb[2].z + srgb[3].z) * 0.25f;
  const int uv = (y >> 1) * out_uvp + x;
  out_uv[uv] = (unsigned char)fminf(fmaxf(roundf(
      128.0f + 224.0f * (-0.1146f * r - 0.3854f * g + 0.5f * b)), 16.0f), 240.0f);
  out_uv[uv + 1] = (unsigned char)fminf(fmaxf(roundf(
      128.0f + 224.0f * (0.5f * r - 0.4542f * g - 0.0458f * b)), 16.0f), 240.0f);
}

__device__ float4 overlay_load_u8(cudaSurfaceObject_t surface, int x, int y,
                                  int width, int height) {
  if (x < 0 || y < 0 || x >= width || y >= height) return make_float4(0, 0, 0, 0);
  uchar4 value;
  surf2Dread(&value, surface, x * (int)sizeof(uchar4), y);
  return make_float4(value.z / 255.0f, value.y / 255.0f,
                     value.x / 255.0f, value.w / 255.0f);
}

extern "C" __global__ void rgba_u8_surface_to_nv12_2x2(
    cudaSurfaceObject_t fg_rgba,
    unsigned char* out_y, int out_yp, unsigned char* out_uv, int out_uvp,
    int width, int height) {
  const int x = ((int)blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int y = ((int)blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (x >= width || y >= height) return;
  const int x1 = min(x + 1, width - 1);
  const int y1 = min(y + 1, height - 1);
  const float4 f00 = overlay_load_u8(fg_rgba, x, y, width, height);
  const float4 f10 = overlay_load_u8(fg_rgba, x1, y, width, height);
  const float4 f01 = overlay_load_u8(fg_rgba, x, y1, width, height);
  const float4 f11 = overlay_load_u8(fg_rgba, x1, y1, width, height);
  const float4 fs[4] = {f00, f10, f01, f11};
  const int xs[4] = {x, x1, x, x1};
  const int ys[4] = {y, y, y1, y1};
  for (int i = 0; i < 4; ++i) {
    const float a = fminf(fmaxf(fs[i].w, 0.0f), 1.0f);
    const float y_value = 16.0f + 219.0f * a *
        (0.2126f * fs[i].x + 0.7152f * fs[i].y + 0.0722f * fs[i].z);
    out_y[ys[i] * out_yp + xs[i]] = (unsigned char)fminf(
        fmaxf(roundf(y_value), 16.0f), 235.0f);
  }
  const float a = (f00.w + f10.w + f01.w + f11.w) * 0.25f;
  const float r = (f00.x*f00.w + f10.x*f10.w + f01.x*f01.w + f11.x*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float g = (f00.y*f00.w + f10.y*f10.w + f01.y*f01.w + f11.y*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float b = (f00.z*f00.w + f10.z*f10.w + f01.z*f01.w + f11.z*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const int uv = (y >> 1) * out_uvp + x;
  out_uv[uv] = (unsigned char)fminf(fmaxf(roundf(
      (1.0f-a) * 128.0f + a * (128.0f + 224.0f * (-0.1146f*r - 0.3854f*g + 0.5f*b))), 16.0f), 240.0f);
  out_uv[uv + 1] = (unsigned char)fminf(fmaxf(roundf(
      (1.0f-a) * 128.0f + a * (128.0f + 224.0f * (0.5f*r - 0.4542f*g - 0.0458f*b))), 16.0f), 240.0f);
}
)CUDA";

[[noreturn]] void fail(const char* operation, const std::string& detail) {
    throw std::runtime_error(std::string(operation) + ": " + detail);
}

void check_cuda(CUresult result, const char* operation) {
    if (result == CUDA_SUCCESS) return;
    const char* name = nullptr;
    const char* description = nullptr;
    cuGetErrorName(result, &name);
    cuGetErrorString(result, &description);
    std::string detail = name ? name : "CUDA error";
    if (description && description != detail) {
        detail += " (";
        detail += description;
        detail += ")";
    }
    fail(operation, detail);
}

void check_nvrtc(nvrtcResult result, const char* operation) {
    if (result == NVRTC_SUCCESS) return;
    fail(operation, nvrtcGetErrorString(result));
}

void ensure_nvrtc_builtins_loaded() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&nvrtcCompileProgram), &info) == 0 ||
        !info.dli_fname) return;
    const auto directory = std::filesystem::path(info.dli_fname).parent_path();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file(error)) continue;
        const auto filename = entry.path().filename().string();
        if (filename.rfind("libnvrtc-builtins.so", 0) != 0) continue;
        (void)dlopen(entry.path().c_str(), RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
        break;
    }
}

std::string get_compiled_nv12_ptx() {
    static std::mutex ptx_mutex;
    static std::string cached_ptx;
    std::lock_guard<std::mutex> lock(ptx_mutex);
    if (!cached_ptx.empty()) {
        return cached_ptx;
    }
    ensure_nvrtc_builtins_loaded();

    nvrtcProgram program{};
    check_nvrtc(nvrtcCreateProgram(&program, kNv12Kernel, "chronon_nv12.cu",
                                   0, nullptr, nullptr), "nvrtcCreateProgram");
    const char* options[] = {
        "--gpu-architecture=" CHRONON3D_NVRTC_ARCHITECTURE};
    const nvrtcResult compile = nvrtcCompileProgram(program, 1, options);
    if (compile != NVRTC_SUCCESS) {
        size_t log_size = 0;
        nvrtcGetProgramLogSize(program, &log_size);
        std::string log(log_size, '\0');
        if (log_size) nvrtcGetProgramLog(program, log.data());
        nvrtcDestroyProgram(&program);
        fail("nvrtcCompileProgram", log);
    }
    size_t ptx_size = 0;
    check_nvrtc(nvrtcGetPTXSize(program, &ptx_size), "nvrtcGetPTXSize");
    std::string ptx(ptx_size, '\0');
    check_nvrtc(nvrtcGetPTX(program, ptx.data()), "nvrtcGetPTX");
    nvrtcDestroyProgram(&program);
    cached_ptx = std::move(ptx);
    return cached_ptx;
}

} // namespace

void CudaNv12SurfaceCompositor::warm_up(CUcontext context) {
    if (!context) fail("CudaNv12SurfaceCompositor::warm_up", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context), "cuCtxSetCurrent(warm_up)");
    const std::string ptx = get_compiled_nv12_ptx();
    CUmodule module = nullptr;
    check_cuda(cuModuleLoadData(&module, ptx.c_str()),
               "cuModuleLoadData(warm_up)");
    check_cuda(cuModuleUnload(module), "cuModuleUnload(warm_up)");
}

CudaNv12SurfaceCompositor::CudaNv12SurfaceCompositor(
    const CudaExternalMemoryInfo& target, CUcontext context)
    : context_(context), surface_u8_(target.cuda_array_format == 2) {
    if (!context_) fail("CudaNv12SurfaceCompositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");

    const std::string ptx = get_compiled_nv12_ptx();

    // The native encoder shares FFmpeg's primary CUDA context.  Surface any
    // earlier asynchronous failure before loading the compositor module so a
    // stale context error cannot be misreported as a PTX loading failure.
    const CUresult module_result = cuModuleLoadData(&module_, ptx.c_str());
    if (module_result != CUDA_SUCCESS) {
        const char* name = nullptr;
        const char* description = nullptr;
        cuGetErrorName(module_result, &name);
        cuGetErrorString(module_result, &description);
        std::string detail = name ? name : "CUDA error";
        if (description) {
            detail += " (";
            detail += description;
            detail += ")";
        }
        detail += "; arch=" CHRONON3D_NVRTC_ARCHITECTURE;
        detail += "; ptx_bytes=" + std::to_string(ptx.size());
        fail("cuModuleLoadData", detail);
    }
    check_cuda(cuModuleGetFunction(&kernel_, module_, "nv12_to_rgba"),
               "cuModuleGetFunction(nv12_to_rgba)");
    check_cuda(cuModuleGetFunction(&p010_kernel_, module_, "p010_to_rgba"),
               "cuModuleGetFunction(p010_to_rgba)");
    check_cuda(cuModuleGetFunction(&direct_nv12_kernel_, module_,
                                   "nv12_composite_overlay_2x2"),
               "cuModuleGetFunction(nv12_composite_overlay_2x2)");
    check_cuda(cuModuleGetFunction(&direct_nv12_batch_kernel_, module_,
                                   "nv12_composite_layer_batch_2x2"),
               "cuModuleGetFunction(nv12_composite_layer_batch_2x2)");
    check_cuda(cuModuleGetFunction(&rgba_to_nv12_kernel_, module_,
                                   "rgba_surface_to_nv12_2x2"),
               "cuModuleGetFunction(rgba_surface_to_nv12_2x2)");
    check_cuda(cuModuleGetFunction(&rgba_u8_to_nv12_kernel_, module_,
                                   "rgba_u8_surface_to_nv12_2x2"),
               "cuModuleGetFunction(rgba_u8_surface_to_nv12_2x2)");
    bridge_ = std::make_unique<CudaVulkanSurfaceBridge>(
        target, context_, stream_);
}

CudaNv12SurfaceCompositor::CudaNv12SurfaceCompositor(CUcontext context)
    : context_(context), surface_u8_(false) {
    if (!context_) fail("CudaNv12SurfaceCompositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    const std::string ptx = get_compiled_nv12_ptx();
    check_cuda(cuModuleLoadData(&module_, ptx.c_str()), "cuModuleLoadData");
    check_cuda(cuModuleGetFunction(&direct_nv12_kernel_, module_,
                                   "nv12_composite_overlay_2x2"),
               "cuModuleGetFunction(nv12_composite_overlay_2x2)");
    check_cuda(cuModuleGetFunction(&direct_nv12_batch_kernel_, module_,
                                   "nv12_composite_layer_batch_2x2"),
               "cuModuleGetFunction(nv12_composite_layer_batch_2x2)");
}

CudaNv12SurfaceCompositor::~CudaNv12SurfaceCompositor() {
    if (context_) (void)cuCtxSetCurrent(context_);
    if (stream_) (void)cuStreamSynchronize(stream_);
    bridge_.reset();
    if (layer_batch_buffer_) (void)cuMemFree(layer_batch_buffer_);
    if (module_) (void)cuModuleUnload(module_);
    if (stream_) (void)cuStreamDestroy(stream_);
}

bool CudaNv12SurfaceCompositor::synchronize() noexcept {
    if (!stream_) return true;
    if (context_) (void)cuCtxSetCurrent(context_);
    return cuStreamSynchronize(stream_) == CUDA_SUCCESS;
}

bool CudaNv12SurfaceCompositor::composite(
    CUdeviceptr y, int y_pitch, CUdeviceptr uv, int uv_pitch,
    std::uint32_t width, std::uint32_t height, CUstream stream,
    CudaYuvFormat format, YuvToRgbParams params) {
    const CUfunction kernel = format == CudaYuvFormat::P010 ? p010_kernel_ : kernel_;
    if (!bridge_ || !kernel || !y || !uv || width == 0 || height == 0) return false;
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    CUstream active = stream ? stream : stream_;
    if (!first_write_) bridge_->wait_for_vulkan(active);
    float y_offset = params.y_offset;
    float y_scale = params.y_scale;
    float uv_offset = params.uv_offset;
    float uv_scale = params.uv_scale;
    float r_v = params.r_v;
    float g_u = params.g_u;
    float g_v = params.g_v;
    float b_u = params.b_u;
    void* args[] = {&y, &y_pitch, &uv, &uv_pitch, nullptr, &width, &height,
                        &y_offset, &y_scale, &uv_offset, &uv_scale,
                        &r_v, &g_u, &g_v, &b_u};
    CUsurfObject surface = bridge_->surface_object();
    args[4] = &surface;
    check_cuda(cuLaunchKernel(kernel, (width + 15) / 16, (height + 15) / 16, 1,
                              16, 16, 1, 0, active, args, nullptr),
               format == CudaYuvFormat::P010
                   ? "cuLaunchKernel(p010_to_rgba)"
                   : "cuLaunchKernel(nv12_to_rgba)");
    bridge_->signal_for_vulkan(active);
    first_write_ = false;
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
        // Zero-copy gate 3: every NV12/P010→RGBA kernel launch is a decoded
        // frame that left the native YUV domain.  The direct-YUV path
        // (composite_direct_nv12*) keeps this counter at zero.
        counters->nv12_to_rgba_frames.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

bool CudaNv12SurfaceCompositor::composite_direct_nv12(
    CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream) {
    if (!bridge_ || !direct_nv12_kernel_ || !bg_y || !bg_uv || !out_y || !out_uv || width == 0 || height == 0) {
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    CUstream active = stream ? stream : stream_;
    if (!first_write_) bridge_->wait_for_vulkan(active);

    CUsurfObject surface = bridge_->surface_object();
    void* args[] = {
        &bg_y, &bg_yp, &bg_uv, &bg_uvp,
        &surface,
        &out_y, &out_yp, &out_uv, &out_uvp,
        &width, &height
    };
    check_cuda(cuLaunchKernel(direct_nv12_kernel_, (width + 31) / 32, (height + 31) / 32, 1,
                              16, 16, 1, 0, active, args, nullptr),
               "cuLaunchKernel(nv12_composite_overlay_2x2)");
    bridge_->signal_for_vulkan(active);
    first_write_ = false;

    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    }
    return true;
}

bool CudaNv12SurfaceCompositor::composite_direct_nv12_batch(
    const runtime::GpuLayerBatch& batch,
    std::span<const CudaLayerResource> resources,
    CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream) {
    if (!direct_nv12_batch_kernel_ || !bg_y || !bg_uv || !out_y || !out_uv ||
        width == 0 || height == 0 || batch.instances.empty()) return false;

    std::vector<DirectYuvLayerHost> host_layers;
    host_layers.reserve(batch.instances.size());
    for (const auto& instance : batch.instances) {
        if (instance.resource_index >= resources.size()) return false;
        const auto& resource = resources[instance.resource_index];
        if (!resource.rgba || resource.pitch_bytes <= 0 ||
            resource.width == 0 || resource.height == 0) return false;
        if (instance.blend != BlendMode::Normal && instance.blend != BlendMode::Add) {
            return false;
        }
        DirectYuvLayerHost layer;
        layer.rgba = resource.rgba;
        layer.pitch = resource.pitch_bytes;
        layer.source_width = static_cast<int>(resource.width);
        layer.source_height = static_cast<int>(resource.height);
        layer.dst_x0 = instance.dst_x0;
        layer.dst_y0 = instance.dst_y0;
        layer.dst_x1 = instance.dst_x1;
        layer.dst_y1 = instance.dst_y1;
        if (layer.dst_x1 <= 1.0f && layer.dst_y1 <= 1.0f &&
            (layer.dst_x1 > layer.dst_x0 || layer.dst_y1 > layer.dst_y0)) {
            layer.dst_x0 *= static_cast<float>(width);
            layer.dst_y0 *= static_cast<float>(height);
            layer.dst_x1 *= static_cast<float>(width);
            layer.dst_y1 *= static_cast<float>(height);
        }
        layer.src_x0 = instance.src_x0;
        layer.src_y0 = instance.src_y0;
        layer.src_x1 = (instance.src_x1 == 0.0f && instance.src_y1 == 0.0f)
            ? 1.0f : instance.src_x1;
        layer.src_y1 = (instance.src_x1 == 0.0f && instance.src_y1 == 0.0f)
            ? 1.0f : instance.src_y1;
        layer.opacity = instance.opacity;
        layer.blend_mode = instance.blend == BlendMode::Add ? 1 : 0;
        host_layers.push_back(layer);
    }

    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    const CUstream active = stream ? stream : stream_;
    const std::size_t bytes = host_layers.size() * sizeof(DirectYuvLayerHost);
    if (layer_batch_capacity_ < bytes) {
        if (layer_batch_buffer_) check_cuda(cuMemFree(layer_batch_buffer_), "cuMemFree(layer batch)");
        check_cuda(cuMemAlloc(&layer_batch_buffer_, bytes), "cuMemAlloc(layer batch)");
        layer_batch_capacity_ = bytes;
    }
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuMemcpyHtoDAsync(
                   layer_batch_buffer_, host_layers.data(), bytes, active),
               "cuMemcpyHtoDAsync(layer batch)");
    CUdeviceptr layers = layer_batch_buffer_;
    int layer_count = static_cast<int>(host_layers.size());
    void* args[] = {&bg_y, &bg_yp, &bg_uv, &bg_uvp, &out_y, &out_yp,
                    &out_uv, &out_uvp, &layers, &layer_count, &width, &height};
    check_cuda(cuLaunchKernel(
                   direct_nv12_batch_kernel_, (width + 31) / 32,
                   (height + 31) / 32, 1, 16, 16, 1, 0, active, args, nullptr),
               "cuLaunchKernel(nv12_composite_layer_batch_2x2)");
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    }
    return true;
}

bool CudaNv12SurfaceCompositor::composite_surface_to_nv12(
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream) {
    const CUfunction kernel = surface_u8_ ? rgba_u8_to_nv12_kernel_ : rgba_to_nv12_kernel_;
    if (!bridge_ || !kernel || !out_y || !out_uv ||
        width == 0 || height == 0) {
        spdlog::error("[nv12_diag] invalid compositor args: bridge={} kernel={} out_y={} out_uv={} width={} height={}",
                      static_cast<void*>(bridge_.get()), static_cast<void*>(kernel),
                      out_y, out_uv, width, height);
        return false;
    }
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    const CUstream active = stream ? stream : stream_;
    // The imported surface was populated by Vulkan immediately before the
    // encoder handoff, including on the first frame. Always consume the
    // Vulkan→CUDA semaphore for this read path.
    bridge_->wait_for_vulkan(active);
    CUsurfObject surface = bridge_->surface_object();
    void* args[] = {&surface, &out_y, &out_yp, &out_uv, &out_uvp,
                    &width, &height};
    check_cuda(cuLaunchKernel(kernel, (width + 31) / 32,
                              (height + 31) / 32, 1, 16, 16, 1, 0, active,
                              args, nullptr),
               "cuLaunchKernel(rgba_surface_to_nv12_2x2)");
    bridge_->signal_for_vulkan(active);
    first_write_ = false;
    if (auto* counters = profiling::g_current_counters) {
        // Zero-copy gate 4: every RGBA→NV12 kernel launch is an encoder
        // staging conversion.  composite_direct_nv12* keeps this at zero.
        counters->rgba_to_nv12_frames.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

} // namespace chronon3d::backends::vulkan

#endif
