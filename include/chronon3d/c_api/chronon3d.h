#pragma once

#include <stdint.h>

#define CHRONON3D_API __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chronon_engine chronon_engine;
typedef struct chronon_plan chronon_plan;

typedef enum chronon_status {
    CHRONON_OK = 0,
    CHRONON_ERROR_UNKNOWN = 1,
    CHRONON_ERROR_INVALID_ARGUMENT = 2,
    CHRONON_ERROR_PARSE_FAILED = 3,
    CHRONON_ERROR_RENDER_FAILED = 4,
    CHRONON_ERROR_IO_FAILED = 5
    ,CHRONON_ERROR_UNSUPPORTED = 6
    ,CHRONON_ERROR_CANCELLED = 7
    ,CHRONON_ERROR_ABI_MISMATCH = 8
} chronon_status;

CHRONON3D_API const char* chronon_version_string(void);

typedef struct chronon_engine_config {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* assets_root;
    uint32_t flags;
} chronon_engine_config;

typedef struct chronon_frame_buffer {
    void* data;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format;
} chronon_frame_buffer;

// V1 frame buffers point into storage owned by chronon_engine. Only one such
// buffer is valid at a time: the next render may replace it, and
// chronon_buffer_free releases it. Calls on the same engine must be serialized.

typedef struct chronon_render_callbacks {
    void (*progress)(void* user, uint64_t current_frame, uint64_t total_frames);
    int (*is_cancelled)(void* user);
    void* user;
} chronon_render_callbacks;

typedef struct chronon_error_info {
    uint32_t struct_size;
    chronon_status status;
    // Valid until the next API call on the same thread. The pointer is
    // implementation-owned and must not be freed by the caller.
    const char* message;
} chronon_error_info;

typedef struct chronon_frame_info {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format;
    uint64_t size;
} chronon_frame_info;

CHRONON3D_API uint32_t chronon_abi_version(void);
CHRONON3D_API chronon_engine* chronon_engine_create(
    const chronon_engine_config* config);
// V2 reports configuration failures through out_error instead of returning
// nullptr without a diagnostic. Engine objects and plans are not thread-safe;
// serialize calls made against the same handle.
CHRONON3D_API chronon_status chronon_engine_create_v2(
    const chronon_engine_config* config, chronon_engine** out_engine,
    chronon_error_info* out_error);
CHRONON3D_API void chronon_engine_destroy(chronon_engine* engine);
CHRONON3D_API const char* chronon_engine_last_error(chronon_engine* engine);

CHRONON3D_API chronon_status chronon_plan_compile_json(
    chronon_engine* engine, const char* json, chronon_plan** out_plan);
CHRONON3D_API chronon_status chronon_plan_compile_json_n(
    chronon_engine* engine, const char* json, uint64_t json_size,
    chronon_plan** out_plan);
CHRONON3D_API void chronon_plan_destroy(chronon_plan* plan);

CHRONON3D_API chronon_status chronon_render_frame(
    chronon_engine* engine, const chronon_plan* plan, uint64_t frame,
    chronon_frame_buffer* out_buffer);

// V2 writes into caller-owned storage. The engine retains no pointer to
// destination; destination_size must fit the rendered row stride times height.
CHRONON3D_API chronon_status chronon_render_frame_into(
    chronon_engine* engine, const chronon_plan* plan, uint64_t frame,
    void* destination, uint64_t destination_size, chronon_frame_info* out_info);

CHRONON3D_API chronon_status chronon_render_file(
    chronon_engine* engine, const chronon_plan* plan,
    const char* output_path, uint64_t start_frame, uint64_t end_frame,
    uint32_t fps_num, uint32_t fps_den,
    const chronon_render_callbacks* callbacks);

CHRONON3D_API void chronon_buffer_free(chronon_engine* engine,
                                       chronon_frame_buffer* buffer);

#ifdef __cplusplus
}
#endif
