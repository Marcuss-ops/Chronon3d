// Minimal raw NVENC proof: CUDA NV12 device pointer -> nvEncodeAPI bitstream.
#include <cuda.h>
#include <ffnvcodec/nvEncodeAPI.h>

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {
[[noreturn]] void fail(const char* what, NVENCSTATUS status = NV_ENC_SUCCESS) {
  std::fprintf(stderr, "NVENC_RAW_FAIL: %s status=%d\n", what, static_cast<int>(status));
  std::exit(1);
}
void cu_ok(CUresult r, const char* what) { if (r != CUDA_SUCCESS) fail(what, static_cast<NVENCSTATUS>(r)); }
void nv_ok(NVENCSTATUS r, const char* what) { if (r != NV_ENC_SUCCESS && r != NV_ENC_ERR_NEED_MORE_INPUT) fail(what, r); }
}

int main() {
  CUdevice device{}; CUcontext context{}; CUdeviceptr frame{}; void* library = nullptr; void* encoder = nullptr;
  NV_ENC_REGISTERED_PTR registered{}; NV_ENC_OUTPUT_PTR bitstream{}; NV_ENC_INPUT_PTR mapped{};
  try {
    constexpr uint32_t width = 1920, height = 1080;
    cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&device, 0), "cuDeviceGet");
#if defined(CUDA_VERSION) && CUDA_VERSION >= 13000
    cu_ok(cuCtxCreate(&context, nullptr, 0, device), "cuCtxCreate");
#else
    cu_ok(cuCtxCreate(&context, 0, device), "cuCtxCreate");
#endif
    size_t pitch = 0; cu_ok(cuMemAllocPitch(&frame, &pitch, width, height + height / 2, 16), "cuMemAllocPitch");
    cu_ok(cuMemsetD8(frame, 16, pitch * height), "clear Y");
    cu_ok(cuMemsetD8(frame + pitch * height, 128, pitch * (height / 2)), "clear UV");

    library = dlopen("libnvidia-encode.so.1", RTLD_NOW | RTLD_LOCAL); if (!library) fail("dlopen(libnvidia-encode)");
    auto create = reinterpret_cast<decltype(&NvEncodeAPICreateInstance)>(dlsym(library, "NvEncodeAPICreateInstance"));
    if (!create) fail("dlsym(NvEncodeAPICreateInstance)");
    NV_ENCODE_API_FUNCTION_LIST api{}; api.version = NV_ENCODE_API_FUNCTION_LIST_VER; nv_ok(create(&api), "NvEncodeAPICreateInstance");
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{}; open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER; open.deviceType = NV_ENC_DEVICE_TYPE_CUDA; open.device = context; open.apiVersion = NVENCAPI_VERSION;
    nv_ok(api.nvEncOpenEncodeSessionEx(&open, &encoder), "nvEncOpenEncodeSessionEx");

    NV_ENC_PRESET_CONFIG preset{}; preset.version = NV_ENC_PRESET_CONFIG_VER; preset.presetCfg.version = NV_ENC_CONFIG_VER;
    NV_ENC_INITIALIZE_PARAMS init{}; init.version = NV_ENC_INITIALIZE_PARAMS_VER; init.encodeGUID = NV_ENC_CODEC_H264_GUID; init.presetGUID = NV_ENC_PRESET_P1_GUID; init.encodeWidth = width; init.encodeHeight = height; init.darWidth = width; init.darHeight = height; init.frameRateNum = 24; init.frameRateDen = 1; init.enablePTD = 1; init.enableEncodeAsync = 0; init.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;
    nv_ok(api.nvEncGetEncodePresetConfigEx(encoder, init.encodeGUID, init.presetGUID, init.tuningInfo, &preset), "nvEncGetEncodePresetConfigEx");
    init.encodeConfig = &preset.presetCfg;
    nv_ok(api.nvEncInitializeEncoder(encoder, &init), "nvEncInitializeEncoder");

    NV_ENC_REGISTER_RESOURCE resource{}; resource.version = NV_ENC_REGISTER_RESOURCE_VER; resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR; resource.width = width; resource.height = height; resource.pitch = static_cast<uint32_t>(pitch); resource.resourceToRegister = reinterpret_cast<void*>(frame); resource.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12; resource.bufferUsage = NV_ENC_INPUT_IMAGE;
    nv_ok(api.nvEncRegisterResource(encoder, &resource), "nvEncRegisterResource"); registered = resource.registeredResource;
    NV_ENC_CREATE_BITSTREAM_BUFFER create_bs{}; create_bs.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER; nv_ok(api.nvEncCreateBitstreamBuffer(encoder, &create_bs), "nvEncCreateBitstreamBuffer"); bitstream = create_bs.bitstreamBuffer;
    NV_ENC_MAP_INPUT_RESOURCE map{}; map.version = NV_ENC_MAP_INPUT_RESOURCE_VER; map.registeredResource = registered; nv_ok(api.nvEncMapInputResource(encoder, &map), "nvEncMapInputResource"); mapped = map.mappedResource;
    NV_ENC_PIC_PARAMS pic{}; pic.version = NV_ENC_PIC_PARAMS_VER; pic.inputWidth = width; pic.inputHeight = height; pic.inputPitch = static_cast<uint32_t>(pitch); pic.inputBuffer = mapped; pic.outputBitstream = bitstream; pic.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12; pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME; pic.encodePicFlags = NV_ENC_PIC_FLAG_OUTPUT_SPSPPS; pic.inputTimeStamp = 0;
    NVENCSTATUS encoded = api.nvEncEncodePicture(encoder, &pic); if (encoded != NV_ENC_SUCCESS && encoded != NV_ENC_ERR_NEED_MORE_INPUT) nv_ok(encoded, "nvEncEncodePicture");
    NV_ENC_LOCK_BITSTREAM lock{}; lock.version = NV_ENC_LOCK_BITSTREAM_VER; lock.outputBitstream = bitstream; nv_ok(api.nvEncLockBitstream(encoder, &lock), "nvEncLockBitstream");
    std::printf("NVENC_RAW_PASS bytes=%u pitch=%zu\n", lock.bitstreamSizeInBytes, pitch); nv_ok(api.nvEncUnlockBitstream(encoder, bitstream), "nvEncUnlockBitstream"); api.nvEncUnmapInputResource(encoder, mapped); api.nvEncDestroyBitstreamBuffer(encoder, bitstream); api.nvEncUnregisterResource(encoder, registered); api.nvEncDestroyEncoder(encoder); cuMemFree(frame); cuCtxDestroy(context); dlclose(library); return 0;
  } catch (...) { fail("unexpected exception"); }
}
