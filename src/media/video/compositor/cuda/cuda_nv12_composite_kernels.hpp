// NVRTC source fragment: composite operations.
#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
namespace chronon3d::media::video::cuda_nv12_detail {
inline constexpr char kNv12CompositeKernel[] = R"CUDA(extern "C" __global__ void nv12_composite_overlay(
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
  unsigned int shadow_color;
  float shadow_opacity;
  float shadow_dx;
  float shadow_dy;
  float shadow_blur;
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

// ── Canonical single MTSDF drop shadow ──────────────────────────────────────
// A drop shadow is the coverage of the silhouette offset by the authored
// vector and softened by the authored blur. The prepared MTSDF atlas already
// encodes that silhouette as a signed distance, so the DirectYUV lane lowers a
// shadow by resampling the SAME field: no second rasterization, no RGBA
// intermediate and no per-frame asset rebuild.

__device__ inline bool direct_layer_has_shadow(const DirectYuvLayer& layer) {
  // resource_kind 2 is the prepared MTSDF text atlas; only it carries a
  // signed-distance field to resample.
  return layer.resource_kind == 2 && layer.shadow_color != 0u &&
         layer.shadow_opacity > 0.0f;
}

__device__ inline float direct_mtsdf_band_dest(const DirectYuvLayer& layer) {
  const float source_scale = 0.5f *
      (fabsf(layer.dst_x1 - layer.dst_x0) / fmaxf((float)layer.source_width, 1.0f) +
       fabsf(layer.dst_y1 - layer.dst_y0) / fmaxf((float)layer.source_height, 1.0f));
  const float range = layer.distance_range_px > 0.0f
      ? layer.distance_range_px : 16.0f;
  return range * source_scale;
}

__device__ float direct_mtsdf_alpha_at(const DirectYuvLayer& layer,
                                        int sx, int sy) {
  sx = min(max(sx, 0), layer.source_width - 1);
  sy = min(max(sy, 0), layer.source_height - 1);
  const char* row = (const char*)layer.rgba + sy * layer.pitch;
  const unsigned int packed = ((const unsigned int*)row)[sx];
  return (float)((packed >> 24u) & 0xFFu) / 255.0f;
}

__device__ float direct_mtsdf_signed_distance(const DirectYuvLayer& layer,
                                              float dst_x, float dst_y) {
  const float u = layer.src_x0 + (layer.src_x1 - layer.src_x0) *
      (dst_x - layer.dst_x0) / fmaxf(layer.dst_x1 - layer.dst_x0, 1.0f);
  const float v = layer.src_y0 + (layer.src_y1 - layer.src_y0) *
      (dst_y - layer.dst_y0) / fmaxf(layer.dst_y1 - layer.dst_y0, 1.0f);
  // Outside the prepared plane there is no silhouette, so the field reads as
  // fully outside (alpha 0 → below the outline).
  if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f) return 0.0f;

  // Sample in texel-centre space and clamp to this glyph's own source rect.
  // The old floor() read was nearest-neighbour and made scaled DirectYUV text
  // shimmer. Four taps keep the fast lane on the same prepared MTSDF payload
  // while preventing a neighbouring glyph from bleeding at the rect edge.
  const float tx = u * (float)layer.source_width - 0.5f;
  const float ty = v * (float)layer.source_height - 0.5f;
  const int x0 = min(max((int)floorf(tx), 0), layer.source_width - 1);
  const int y0 = min(max((int)floorf(ty), 0), layer.source_height - 1);
  const int x1 = min(x0 + 1, layer.source_width - 1);
  const int y1 = min(y0 + 1, layer.source_height - 1);
  const float fx = fminf(fmaxf(tx - floorf(tx), 0.0f), 1.0f);
  const float fy = fminf(fmaxf(ty - floorf(ty), 0.0f), 1.0f);
  const float top = direct_mtsdf_alpha_at(layer, x0, y0) * (1.0f - fx) +
                    direct_mtsdf_alpha_at(layer, x1, y0) * fx;
  const float bottom = direct_mtsdf_alpha_at(layer, x0, y1) * (1.0f - fx) +
                       direct_mtsdf_alpha_at(layer, x1, y1) * fx;
  return top * (1.0f - fy) + bottom * fy;
}

__device__ float direct_mtsdf_shadow_tap(const DirectYuvLayer& layer,
                                         float dst_x, float dst_y,
                                         float width_dest, float band_dest) {
  const float distance_dest =
      (direct_mtsdf_signed_distance(layer, dst_x, dst_y) - 0.5f) * band_dest;
  return fminf(fmaxf(distance_dest / fmaxf(width_dest, 1e-4f) + 0.5f, 0.0f), 1.0f);
}

__device__ float direct_mtsdf_shadow_coverage(const DirectYuvLayer& layer,
                                              int x, int y, float band_dest) {
  const float base_x = (float)x - layer.shadow_dx;
  const float base_y = (float)y - layer.shadow_dy;
  const float blur = fmaxf(layer.shadow_blur, 0.0f);
  if (blur <= 1e-4f) {
    // Blur 0 keeps the canonical hard offset silhouette.
    const float distance_dest =
        (direct_mtsdf_signed_distance(layer, base_x, base_y) - 0.5f) * band_dest;
    return distance_dest > 0.0f ? 1.0f : 0.0f;
  }
  // The prepared field can only express an edge as soft as its own distance
  // band (`band_dest`); the value saturates outside it. The authored blur is
  // therefore completed by a normalized binomial 3x3 kernel whose taps sit at
  // half the authored radius: the visible softness tracks the authored blur
  // instead of stopping at the field range, and the taps are still samples of
  // the SAME prepared field — no second rasterization, no RGBA intermediate.
  const float width = fminf(blur, band_dest);
  const float step = 0.5f * blur;
  float coverage = 0.25f *
      direct_mtsdf_shadow_tap(layer, base_x, base_y, width, band_dest);
  // Binomial 3x3: centre 4/16, edge 2/16, corner 1/16 (0.25 / 0.125 / 0.0625).
  float axis = direct_mtsdf_shadow_tap(layer, base_x + step, base_y, width, band_dest) +
               direct_mtsdf_shadow_tap(layer, base_x - step, base_y, width, band_dest) +
               direct_mtsdf_shadow_tap(layer, base_x, base_y + step, width, band_dest) +
               direct_mtsdf_shadow_tap(layer, base_x, base_y - step, width, band_dest);
  float corner = direct_mtsdf_shadow_tap(layer, base_x + step, base_y + step, width, band_dest) +
                 direct_mtsdf_shadow_tap(layer, base_x + step, base_y - step, width, band_dest) +
                 direct_mtsdf_shadow_tap(layer, base_x - step, base_y + step, width, band_dest) +
                 direct_mtsdf_shadow_tap(layer, base_x - step, base_y - step, width, band_dest);
  coverage += 0.125f * axis + 0.0625f * corner;
  return coverage;
}

__device__ float4 direct_mtsdf_shadow_sample(const DirectYuvLayer& layer,
                                             int x, int y) {
  const float coverage = direct_mtsdf_shadow_coverage(
      layer, x, y, direct_mtsdf_band_dest(layer));
  const float4 shadow = unpack_direct_color(layer.shadow_color);
  // Straight RGB plus alpha: the painter premultiplies exactly once, the same
  // convention the stroke and fill branches use.
  return make_float4(shadow.x, shadow.y, shadow.z,
                     shadow.w * layer.shadow_opacity * coverage);
}

__device__ inline bool direct_layer_shadow_in_bounds(const DirectYuvLayer& layer,
                                                     int x, int y) {
  // The authored radius plus the prepared field's own edge softness, so the
  // faint outer falloff is not clipped by the glyph box.
  const float halo = 0.5f * fmaxf(layer.shadow_blur, 0.0f) +
      0.5f * direct_mtsdf_band_dest(layer) + 0.5f;
  const float x0 = fminf(layer.dst_x0, layer.dst_x0 + layer.shadow_dx - halo);
  const float x1 = fmaxf(layer.dst_x1, layer.dst_x1 + layer.shadow_dx + halo);
  const float y0 = fminf(layer.dst_y0, layer.dst_y0 + layer.shadow_dy - halo);
  const float y1 = fmaxf(layer.dst_y1, layer.dst_y1 + layer.shadow_dy + halo);
  return (float)x >= x0 && (float)x < x1 && (float)y >= y0 && (float)y < y1;
}

// DirectYUV consumes canonical linear resources and emits SDR video by
// default. Keep this helper next to the direct batch kernels so the fast path
// cannot silently bypass the same colour boundary used by RGBA conversion.
__device__ inline float direct_encode_transfer_channel(float linear,
                                                       unsigned int transfer) {
  if (transfer == 0u) return linear;
  if (linear <= 0.0f) return 0.0f;
  if (linear >= 1.0f) return 1.0f;
  if (transfer == 2u) return powf(linear, 1.0f / 2.4f);
  if (transfer == 3u) {
    // SMPTE ST 2084 OETF, with the canonical linear value normalized to the
    // 10,000-nit reference white domain.
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    const float p = powf(linear, m1);
    return powf((c1 + c2 * p) / (1.0f + c3 * p), m2);
  }
  if (transfer == 4u) {
    // ARIB STD-B67 OETF, scene-linear input normalized to reference white.
    constexpr float a = 0.17883277f;
    constexpr float b = 0.28466892f;
    constexpr float c = 0.55991073f;
    return linear <= (1.0f / 12.0f)
        ? sqrtf(3.0f * linear)
        : a * logf(12.0f * linear - b) + c;
  }
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
    const float a_channel = direct_mtsdf_signed_distance(layer, (float)x, (float)y);
    // The alpha lane is the canonical signed distance produced by the
    // MTSDF generator.  RGB are edge-coloured distances and may not be
    // median-combinable for fonts whose contour starts/ends on arbitrary
    // edges (Montserrat exposed this as a dotted/fragmented watermark).
    // Using the true distance keeps DirectYUV text deterministic and avoids
    // falling back to the expensive RGBA compositor for a simple watermark.
    const float source_scale = 0.5f *
        (fabsf(layer.dst_x1 - layer.dst_x0) / (float)layer.source_width +
         fabsf(layer.dst_y1 - layer.dst_y0) / (float)layer.source_height);
    const float range = layer.distance_range_px > 0.0f
        ? layer.distance_range_px : 16.0f;
    // Convert the normalized atlas distance into destination pixels before
    // resolving coverage. The 0.5px ramp keeps thin glyph edges intact through
    // NV12 chroma subsampling and video encoding.
    const float field_px = (a_channel - 0.5f) * range * source_scale;
    const float fill_alpha = fminf(fmaxf(field_px + 0.5f, 0.0f), 1.0f);
    // The shadow is painted first — under the stroke, under the fill — from
    // the same prepared distance field.
    float4 src = direct_layer_has_shadow(layer)
        ? direct_mtsdf_shadow_sample(layer, x, y)
        : make_float4(0, 0, 0, 0);
    if (layer.stroke_width > 0.0f && layer.stroke_color != 0u) {
      const float stroke_alpha = fminf(fmaxf(
          field_px + 0.5f * layer.stroke_width + 0.5f, 0.0f), 1.0f);
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
    float4 src;
    if (x >= layer.dst_x0 && x < layer.dst_x1 &&
        y >= layer.dst_y0 && y < layer.dst_y1) {
      src = direct_layer_load(layer, x, y, width, height);
    } else if (direct_layer_has_shadow(layer) &&
               direct_layer_shadow_in_bounds(layer, x, y)) {
      // The blur envelope is authored OUTSIDE the glyph box: composite the
      // shadow alone there instead of letting the layer rect cut it off.
      src = direct_mtsdf_shadow_sample(layer, x, y);
    } else {
      continue;
    }
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
)CUDA";
} // namespace chronon3d::media::video::cuda_nv12_detail
#endif
