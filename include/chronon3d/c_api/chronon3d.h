#pragma once

#include <stdint.h>

#define CHRONON3D_API __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chronon_context chronon_context;
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
} chronon_status;

typedef struct chronon_render_options {
    uint32_t width;
    uint32_t height;
    uint32_t frame;
    uint32_t fps;
    uint32_t flags;
} chronon_render_options;

CHRONON3D_API chronon_context* chronon_create_context(void);
CHRONON3D_API void chronon_destroy_context(chronon_context* ctx);

CHRONON3D_API chronon_status chronon_render_json_file(
    chronon_context* ctx,
    const char* json_path,
    const char* output_png_path,
    const chronon_render_options* options
);

CHRONON3D_API chronon_status chronon_render_json_string(
    chronon_context* ctx,
    const char* json_string,
    const char* output_png_path,
    const chronon_render_options* options
);

CHRONON3D_API const char* chronon_last_error(chronon_context* ctx);

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

typedef struct chronon_render_callbacks {
    void (*progress)(void* user, uint64_t current_frame, uint64_t total_frames);
    int (*is_cancelled)(void* user);
    void* user;
} chronon_render_callbacks;

CHRONON3D_API uint32_t chronon_abi_version(void);
CHRONON3D_API chronon_engine* chronon_engine_create(
    const chronon_engine_config* config);
CHRONON3D_API void chronon_engine_destroy(chronon_engine* engine);
CHRONON3D_API const char* chronon_engine_last_error(chronon_engine* engine);

CHRONON3D_API chronon_status chronon_plan_compile_json(
    chronon_engine* engine, const char* json, chronon_plan** out_plan);
CHRONON3D_API void chronon_plan_destroy(chronon_plan* plan);

CHRONON3D_API chronon_status chronon_render_frame(
    chronon_engine* engine, const chronon_plan* plan, uint64_t frame,
    chronon_frame_buffer* out_buffer);

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
