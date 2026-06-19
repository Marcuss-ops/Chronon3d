#pragma once

#include <stdint.h>

#ifdef _WIN32
  #define CHRONON3D_API __declspec(dllimport)
#else
  #define CHRONON3D_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chronon_context chronon_context;

typedef enum chronon_status {
    CHRONON_OK = 0,
    CHRONON_ERROR_UNKNOWN = 1,
    CHRONON_ERROR_INVALID_ARGUMENT = 2,
    CHRONON_ERROR_PARSE_FAILED = 3,
    CHRONON_ERROR_RENDER_FAILED = 4,
    CHRONON_ERROR_IO_FAILED = 5
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

#ifdef __cplusplus
}
#endif
