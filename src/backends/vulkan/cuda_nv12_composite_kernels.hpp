// cuda_nv12_composite_kernels.hpp — embedded CUDA source compiled at runtime
// with NVRTC. Data-only header: the kernels implement NV12/P010→RGBA
// conversion, RGBA overlay blending and RGBA→NV12/P010 staging conversion.
// Split out of cuda_nv12_surface_compositor.cpp.

#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

namespace chronon3d::backends::vulkan::cuda_nv12_detail {

inline constexpr char kNv12Kernel[] = R"CUDA(
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

} // namespace chronon3d::backends::vulkan::cuda_nv12_detail

#endif // CHRONON3D_ENABLE_CUDA_INTEROP
