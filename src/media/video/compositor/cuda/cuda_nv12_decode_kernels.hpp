// NVRTC source fragment: decode operations.
#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
namespace chronon3d::media::video::cuda_nv12_detail {
inline constexpr char kNv12DecodeKernel[] = R"CUDA(
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

)CUDA";
} // namespace chronon3d::media::video::cuda_nv12_detail
#endif
