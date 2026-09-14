// cuda_nv12_kernels.hpp — embedded CUDA source compiled at runtime
// with NVRTC. Data-only header: the kernels implement NV12/P010→RGBA
// conversion, RGBA overlay blending and RGBA→NV12/P010 staging conversion.
// Split out of cuda_nv12_compositor.cpp.

#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

namespace chronon3d::media::video::cuda_nv12_detail {

inline constexpr char kNv12Kernel[] = R"CUDA(
__device__ inline float decode_transfer_channel(float encoded,
                                                unsigned int transfer) {
  if (transfer == 0u) return encoded;
  encoded = fminf(fmaxf(encoded, 0.0f), 1.0f);
  return (encoded <= 0.04045f)
      ? (encoded / 12.92f)
      : powf((encoded + 0.055f) / 1.055f, 2.4f);
}

extern "C" __global__ void nv12_to_rgba(
    const unsigned char* y, int yp, const unsigned char* uv, int uvp,
    cudaSurfaceObject_t out, int width, int height,
    float y_offset, float y_scale, float uv_offset, float uv_scale,
    float r_v, float g_u, float g_v, float b_u,
    unsigned int transfer_decode) {
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
  r = decode_transfer_channel(r, transfer_decode);
  g = decode_transfer_channel(g, transfer_decode);
  b = decode_transfer_channel(b, transfer_decode);
  surf2Dwrite(make_float4(r, g, b, 1.0f), out, x * (int)sizeof(float4), yy);
}

extern "C" __global__ void p010_to_rgba(
    const unsigned short* y, int yp, const unsigned short* uv, int uvp,
    cudaSurfaceObject_t out, int width, int height,
    float y_offset, float y_scale, float uv_offset, float uv_scale,
    float r_v, float g_u, float g_v, float b_u,
    unsigned int transfer_decode) {
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
  r = decode_transfer_channel(r, transfer_decode);
  g = decode_transfer_channel(g, transfer_decode);
  b = decode_transfer_channel(b, transfer_decode);
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
  int resource_kind;
  float distance_range_px;
  unsigned int color;
  unsigned int stroke_color;
  float stroke_width;
};

__device__ float4 unpack_direct_color(unsigned int color) {
  return make_float4((float)(color & 0xFFu) / 255.0f,
                     (float)((color >> 8u) & 0xFFu) / 255.0f,
                     (float)((color >> 16u) & 0xFFu) / 255.0f,
                     (float)((color >> 24u) & 0xFFu) / 255.0f);
}

__device__ float direct_median3(float a, float b, float c) {
  return max(min(a, b), min(max(a, b), c));
}

// DirectYUV consumes canonical linear resources and emits SDR video by
// default. Keep this helper next to the direct batch kernels so the fast path
// cannot silently bypass the same colour boundary used by RGBA conversion.
__device__ inline float direct_encode_transfer_channel(float linear,
                                                       unsigned int transfer) {
  if (transfer == 0u) return linear;
  if (linear <= 0.0f) return 0.0f;
  if (linear >= 1.0f) return 1.0f;
  return (linear <= 0.0031308f)
      ? (linear * 12.92f)
      : (1.055f * powf(linear, 1.0f / 2.4f) - 0.055f);
}

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
  if (layer.resource_kind == 2) {
    // MTSDF resources use the same bit-preserving RGBA8-in-a-float-cell
    // upload ABI as the Vulkan atlas. CUDA decodes the field at the final
    // sample position, so DirectYUV never creates a CPU RGBA texture.
    const unsigned int packed = ((const unsigned int*)row)[sx];
    const float r = (float)(packed & 0xFFu) / 255.0f;
    const float g = (float)((packed >> 8u) & 0xFFu) / 255.0f;
    const float b = (float)((packed >> 16u) & 0xFFu) / 255.0f;
    const float a_channel = (float)((packed >> 24u) & 0xFFu) / 255.0f;
    // The alpha lane is the canonical signed distance produced by the
    // MTSDF generator.  RGB are edge-coloured distances and may not be
    // median-combinable for fonts whose contour starts/ends on arbitrary
    // edges (Montserrat exposed this as a dotted/fragmented watermark).
    // Using the true distance keeps DirectYUV text deterministic and avoids
    // falling back to the expensive RGBA compositor for a simple watermark.
    const float signed_distance = a_channel;
    const float source_scale = 0.5f *
        (fabsf(layer.dst_x1 - layer.dst_x0) / (float)layer.source_width +
         fabsf(layer.dst_y1 - layer.dst_y0) / (float)layer.source_height);
    const float range = layer.distance_range_px > 0.0f
        ? layer.distance_range_px : 8.0f;
    // Keep the fill coverage independent of the destination scale. The
    // watermark atlas is already generated at the canonical 64px profile;
    // applying the downscale factor to the distance field collapses ordinary
    // glyph interiors to a sparse one-pixel contour at API watermark sizes.
    // The signed-distance threshold is stable under nearest sampling and
    // preserves the complete glyph silhouette in the NV12 fast path.
    const float fill_alpha = signed_distance > 0.5f ? 1.0f : 0.0f;
    float4 src = make_float4(0, 0, 0, 0);
    if (layer.stroke_width > 0.0f && layer.stroke_color != 0u) {
      const float stroke_alpha = fminf(fmaxf(
          ((a_channel - 0.5f) * range + layer.stroke_width) *
              source_scale + 0.5f, 0.0f), 1.0f);
      const float4 stroke = unpack_direct_color(layer.stroke_color);
      src = make_float4(stroke.x, stroke.y, stroke.z, stroke.w * stroke_alpha);
    }
    const float4 fill = unpack_direct_color(layer.color);
    const float4 fill_src = make_float4(fill.x, fill.y, fill.z,
                                        fill.w * fill_alpha);
    src.x = fill_src.x * fill_src.w + src.x * (1.0f - fill_src.w);
    src.y = fill_src.y * fill_src.w + src.y * (1.0f - fill_src.w);
    src.z = fill_src.z * fill_src.w + src.z * (1.0f - fill_src.w);
    src.w = fill_src.w + src.w * (1.0f - fill_src.w);
    return src;
  }
  float4 src = ((const float4*)row)[sx];
  if (layer.resource_kind == 3) {
    // ImageCache uploads canonical linear premultiplied RGBA. The painter
    // below operates on straight RGB plus alpha, so undo premultiplication at
    // this single resource boundary and nowhere else.
    const float a = fminf(fmaxf(src.w, 0.0f), 1.0f);
    if (a > 1e-5f) {
      src.x /= a;
      src.y /= a;
      src.z /= a;
    } else {
      src.x = src.y = src.z = 0.0f;
    }
  }
  return src;
}

__device__ float4 direct_layer_pixel(const DirectYuvLayer* layers, int count,
                                     int x, int y, int width, int height) {
  float4 accum = make_float4(0, 0, 0, 0);
  for (int i = 0; i < count; ++i) {
    const DirectYuvLayer& layer = layers[i];
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
    const DirectYuvLayer* layers, int layer_count, int width, int height,
    unsigned int transfer_encode) {
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
    const float r = direct_encode_transfer_channel(fs[i].x, transfer_encode);
    const float g = direct_encode_transfer_channel(fs[i].y, transfer_encode);
    const float b = direct_encode_transfer_channel(fs[i].z, transfer_encode);
    const float y_fg = 16.0f + 219.0f *
        (0.2126f * r + 0.7152f * g + 0.0722f * b);
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
  const float r_linear = (f00.x*f00.w + f10.x*f10.w + f01.x*f01.w + f11.x*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float g_linear = (f00.y*f00.w + f10.y*f10.w + f01.y*f01.w + f11.y*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float b_linear = (f00.z*f00.w + f10.z*f10.w + f01.z*f01.w + f11.z*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float r = direct_encode_transfer_channel(r_linear, transfer_encode);
  const float g = direct_encode_transfer_channel(g_linear, transfer_encode);
  const float b = direct_encode_transfer_channel(b_linear, transfer_encode);
  const float u = 128.0f + 224.0f * (-0.1146f*r - 0.3854f*g + 0.5000f*b);
  const float v = 128.0f + 224.0f * ( 0.5000f*r - 0.4542f*g - 0.0458f*b);
  out_uv[uv] = (unsigned char)fminf(fmaxf(roundf((1.0f-a)*bg_uv[bg_uv_index] + a*u), 16.0f), 240.0f);
  out_uv[uv+1] = (unsigned char)fminf(fmaxf(roundf((1.0f-a)*bg_uv[bg_uv_index+1] + a*v), 16.0f), 240.0f);
}

// P010 counterpart of the direct batch kernel. P010 stores 10-bit limited
// range samples left-shifted by six in 16-bit words; pitches remain byte
// pitches at the ABI boundary and are converted to uint16 strides here.
extern "C" __global__ void p010_composite_layer_batch_2x2(
    const unsigned short* bg_y, int bg_yp, const unsigned short* bg_uv, int bg_uvp,
    unsigned short* out_y, int out_yp, unsigned short* out_uv, int out_uvp,
    const DirectYuvLayer* layers, int layer_count, int width, int height,
    unsigned int transfer_encode) {
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
  const int bg_y_stride = bg_yp / 2;
  const int out_y_stride = out_yp / 2;
  for (int i = 0; i < 4; ++i) {
    const float a = fminf(fmaxf(fs[i].w, 0.0f), 1.0f);
    const float r = direct_encode_transfer_channel(fs[i].x, transfer_encode);
    const float g = direct_encode_transfer_channel(fs[i].y, transfer_encode);
    const float b = direct_encode_transfer_channel(fs[i].z, transfer_encode);
    const float y_fg = 64.0f + 876.0f *
        (0.2126f * r + 0.7152f * g + 0.0722f * b);
    const float bg = (float)(bg_y[ys[i] * bg_y_stride + xs[i]] >> 6);
    const float value = (1.0f - a) * bg + a * y_fg;
    out_y[ys[i] * out_y_stride + xs[i]] =
        (unsigned short)(fminf(fmaxf(roundf(value), 64.0f), 940.0f) * 64.0f);
  }
  const float a0 = fminf(fmaxf(f00.w, 0.0f), 1.0f);
  const float a1 = fminf(fmaxf(f10.w, 0.0f), 1.0f);
  const float a2 = fminf(fmaxf(f01.w, 0.0f), 1.0f);
  const float a3 = fminf(fmaxf(f11.w, 0.0f), 1.0f);
  const float a_sum = a0 + a1 + a2 + a3;
  const float a = a_sum * 0.25f;
  const int out_uv_stride = out_uvp / 2;
  const int bg_uv_stride = bg_uvp / 2;
  const int uv = (y >> 1) * out_uv_stride + x;
  const int bg_uv_index = (y >> 1) * bg_uv_stride + x;
  if (a <= 0.0f) {
    out_uv[uv] = bg_uv[bg_uv_index];
    out_uv[uv + 1] = bg_uv[bg_uv_index + 1];
    return;
  }
  const float r_linear = (f00.x*a0 + f10.x*a1 + f01.x*a2 + f11.x*a3) /
      fmaxf(a_sum, 1e-6f);
  const float g_linear = (f00.y*a0 + f10.y*a1 + f01.y*a2 + f11.y*a3) /
      fmaxf(a_sum, 1e-6f);
  const float b_linear = (f00.z*a0 + f10.z*a1 + f01.z*a2 + f11.z*a3) /
      fmaxf(a_sum, 1e-6f);
  const float r = direct_encode_transfer_channel(r_linear, transfer_encode);
  const float g = direct_encode_transfer_channel(g_linear, transfer_encode);
  const float b = direct_encode_transfer_channel(b_linear, transfer_encode);
  const float u = 512.0f + 896.0f * (-0.1146f*r - 0.3854f*g + 0.5000f*b);
  const float v = 512.0f + 896.0f * ( 0.5000f*r - 0.4542f*g - 0.0458f*b);
  const float bg_u = (float)(bg_uv[bg_uv_index] >> 6);
  const float bg_v = (float)(bg_uv[bg_uv_index + 1] >> 6);
  out_uv[uv] = (unsigned short)(fminf(fmaxf(roundf((1.0f-a)*bg_u + a*u), 64.0f), 960.0f) * 64.0f);
  out_uv[uv + 1] = (unsigned short)(fminf(fmaxf(roundf((1.0f-a)*bg_v + a*v), 64.0f), 960.0f) * 64.0f);
}

// ── Canonical colour boundary: linear light → target transfer ──────────
//
// The canonical graphics surface is RGBA32F with a LINEAR transfer (BT.709
// primaries, premultiplied alpha). The NV12/P010 encoder target is nonlinear
// BT.709 SDR. The transfer encode therefore belongs at THIS boundary — the
// last step before the encoder — and it is the exact mirror of the decode-side
// EOTF applied when a decoded frame is brought back into the canonical linear
// domain. Deciding it here, from the target, is what stops "the surface is
// already encoded" assumptions from silently darkening every non-video layer.
//
// codes (mirroring the renderer's transfer vocabulary):
//   0 = none           — the surface already carries the target domain
//   1 = sRGB / BT.709  — SDR camera OETF, the NVENC SDR target
// Mirrors chronon3d::Color::to_srgb() so host and device cannot drift.
__device__ inline float encode_transfer_channel(float linear,
                                                unsigned int transfer) {
  if (transfer == 0u) return linear;
  if (linear <= 0.0f) return 0.0f;
  if (linear >= 1.0f) return 1.0f;
  return (linear <= 0.0031308f)
      ? (linear * 12.92f)
      : (1.055f * powf(linear, 1.0f / 2.4f) - 0.055f);
}

extern "C" __global__ void rgba_surface_to_nv12_2x2(
    cudaSurfaceObject_t fg_rgba,
    unsigned char* out_y, int out_yp, unsigned char* out_uv, int out_uvp,
    int width, int height, int region_x0, int region_y0,
    int region_x1, int region_y1, unsigned int transfer) {
  const int x = region_x0 + ((int)blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int y = region_y0 + ((int)blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (x >= region_x1 || y >= region_y1 || x >= width || y >= height) return;
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
    // Linear light in, target transfer out. `transfer == 0` keeps the legacy
    // pass-through for a writer whose target is already linear; every SDR
    // encoder target (NVENC BT.709) requests the OETF.
    srgb[i].x = encode_transfer_channel(fminf(fmaxf(fs[i].x * inv_a, 0.0f), 1.0f), transfer);
    srgb[i].y = encode_transfer_channel(fminf(fmaxf(fs[i].y * inv_a, 0.0f), 1.0f), transfer);
    srgb[i].z = encode_transfer_channel(fminf(fmaxf(fs[i].z * inv_a, 0.0f), 1.0f), transfer);
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
    int width, int height, int region_x0, int region_y0,
    int region_x1, int region_y1) {
  const int x = region_x0 + ((int)blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int y = region_y0 + ((int)blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (x >= region_x1 || y >= region_y1 || x >= width || y >= height) return;
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

} // namespace chronon3d::media::video::cuda_nv12_detail

#endif // CHRONON3D_ENABLE_CUDA_INTEROP
