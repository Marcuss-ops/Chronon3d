// NVRTC source fragment: encode operations.
#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
namespace chronon3d::media::video::cuda_nv12_detail {
inline constexpr char kNv12EncodeKernel[] = R"CUDA(extern "C" __global__ void rgba_surface_to_nv12_2x2(
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
    // NV12 has no alpha plane. The renderer surface is linear, premultiplied
    // RGBA, so its RGB already represents compositing over the video matte
    // (black). Preserve that premultiplied RGB at this boundary; unpremultiply
    // here would make any translucent colour opaque and discard its opacity.
    // `transfer == 0` keeps the pass-through for an already encoded target.
    srgb[i].x = encode_transfer_channel(fminf(fmaxf(fs[i].x, 0.0f), 1.0f), transfer);
    srgb[i].y = encode_transfer_channel(fminf(fmaxf(fs[i].y, 0.0f), 1.0f), transfer);
    srgb[i].z = encode_transfer_channel(fminf(fmaxf(fs[i].z, 0.0f), 1.0f), transfer);
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
#endif
