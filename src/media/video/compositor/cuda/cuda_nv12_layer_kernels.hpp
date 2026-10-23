// NVRTC source fragment: layer operations.
#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
namespace chronon3d::media::video::cuda_nv12_detail {
inline constexpr char kNv12LayerKernel[] = R"CUDA(extern "C" __global__ void nv12_composite_layer_batch_2x2(
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
  if (transfer == 2u) return powf(linear, 1.0f / 2.4f);
  if (transfer == 3u) {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    const float p = powf(linear, m1);
    return powf((c1 + c2 * p) / (1.0f + c3 * p), m2);
  }
  if (transfer == 4u) {
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

)CUDA";
} // namespace chronon3d::media::video::cuda_nv12_detail
#endif
