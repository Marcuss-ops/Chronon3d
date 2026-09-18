#pragma once

#include <stdint.h>

/*
 * Stable public contract shared by Chronon and Chronon3D.
 *
 * This header intentionally contains only opaque handles, fixed-width integer
 * types, POD structs, callbacks, and status values.  It must not include C++
 * runtime, STL, renderer, text, image, video, scene, or cache types.
 */
#if defined(_WIN32)
  #ifdef CHRONON3D_BUILD_DLL
    #define CHRONON_API __declspec(dllexport)
  #else
    #define CHRONON_API __declspec(dllimport)
  #endif
#else
  #define CHRONON_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * Contract zone (ADR-032) — the motion handshake.
 *
 * POD-only vocabulary flowing Motion3D → Chronon at frame boundaries:
 * a layer state carries an opaque content id plus the final compiled
 * transform. The motion side decides WHERE and WHEN; the content side
 * decides WHAT. Neither side sees the other's semantics: no scene graph,
 * no camera authoring types, no keyframes, no STL on this boundary.
 *
 * Field order is part of the ABI: append-only evolution, evergreen
 * `struct_size` as the first field of every versioned record.
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct chronon_engine chronon_engine;
typedef struct chronon_plan chronon_plan;
typedef void (*chronon_log_callback)(int level, const char* component,
                                     const char* message, void* user);

/* Opaque cross-repo identifiers. 0 is reserved and never a valid id. */
typedef uint64_t chronon_content_id;
typedef uint64_t chronon_layer_id;
typedef uint32_t chronon_frame_index;

/* Content axis (mirrors the internal ContentKind taxonomy; values are part
 * of the contract and append-only). */
typedef enum chronon_content_kind {
    CHRONON_CONTENT_NONE = 0,
    CHRONON_CONTENT_IMAGE = 1,
    CHRONON_CONTENT_SHAPE = 2,
    CHRONON_CONTENT_TEXT = 3,
    CHRONON_CONTENT_VIDEO = 4,
    CHRONON_CONTENT_COMPOSITION = 5,
    CHRONON_CONTENT_AUDIO = 6
} chronon_content_kind;

/* Measure handshake (Chronon → Motion3D): what the content IS, in pixels,
 * once resolved. No glyph, JPEG or pixel payload ever crosses here. */
typedef struct chronon_content_metrics {
    uint32_t struct_size;   /* sizeof(chronon_content_metrics) */
    chronon_content_kind kind;
    chronon_content_id content;
    uint32_t width;         /* px */
    uint32_t height;        /* px */
    float anchor_x;        /* normalized 0..1 */
    float anchor_y;         /* normalized 0..1 */
    uint64_t fingerprint;   /* asset revision digest (opaque) */
} chronon_content_metrics;

/* Motion handshake (Motion3D → Chronon): one layer's final compiled state
 * at one frame. `clip_from_local` is the full projection·view·world·local
 * product — Chronon never receives a camera. Row-major storage, column
 * vectors, forward (left-handed) depth 0..1. */
typedef struct chronon_layer_state {
    uint32_t struct_size;   /* sizeof(chronon_layer_state) */
    uint8_t visible;        /* 0 = skip, 1 = draw */
    uint8_t reserved[3];    /* zero-filled, ABI padding discipline */
    chronon_layer_id layer;
    chronon_content_id content;
    float clip_from_local[16];
    float opacity;          /* 0..1, composited multiplicatively up the hierarchy */
    float depth;            /* forward depth of the layer origin, 0..1 (NDC) */
} chronon_layer_state;

typedef enum chronon_status {
    CHRONON_OK = 0,
    CHRONON_ERROR_UNKNOWN = 1,
    CHRONON_ERROR_INVALID_ARGUMENT = 2,
    CHRONON_ERROR_PARSE_FAILED = 3,
    CHRONON_ERROR_RENDER_FAILED = 4,
    CHRONON_ERROR_IO_FAILED = 5,
    CHRONON_ERROR_UNSUPPORTED = 6,
    CHRONON_ERROR_CANCELLED = 7,
    CHRONON_ERROR_ABI_MISMATCH = 8,
    CHRONON_ERROR_BUFFER_TOO_SMALL = 9,
    CHRONON_ERROR_BUSY = 10,
    CHRONON_ERROR_ASSET_CHANGED = 11,
    CHRONON_ERROR_BUDGET_EXCEEDED = 12,
    CHRONON_ERROR_PREFLIGHT_FAILED = 13,
    CHRONON_ERROR_INVALID_PLAN = 14,
    CHRONON_ERROR_UNSUPPORTED_SCHEMA = 15,
    CHRONON_ERROR_ASSET_NOT_FOUND = 16,
    CHRONON_ERROR_DECODE_FAILED = 17,
    CHRONON_ERROR_ENCODE_FAILED = 18,
    CHRONON_ERROR_OUT_OF_MEMORY = 19
} chronon_status;

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

typedef struct chronon_error_info {
    uint32_t struct_size;
    chronon_status status;
    const char* message;
    const char* code;
    const char* component;
    const char* node_id;
    const char* asset;
} chronon_error_info;

typedef struct chronon_frame_info {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format;
    uint64_t size;
} chronon_frame_info;

/* ABI major 2: additive structured errors, explicit JSON lengths, caller-owned
 * frame output, and per-engine busy-state serialization. */
CHRONON_API uint32_t chronon_abi_version(void);
CHRONON_API const char* chronon_version_string(void);
CHRONON_API const char* chronon_status_name(chronon_status status);

CHRONON_API chronon_engine* chronon_engine_create(
    const chronon_engine_config* config);
CHRONON_API chronon_status chronon_engine_create_v2(
    const chronon_engine_config* config, chronon_engine** out_engine,
    chronon_error_info* out_error);
CHRONON_API void chronon_engine_destroy(chronon_engine* engine);
CHRONON_API const char* chronon_engine_last_error(chronon_engine* engine);
CHRONON_API chronon_status chronon_engine_last_error_info(
    chronon_engine* engine, chronon_error_info* out);
CHRONON_API chronon_status chronon_engine_set_log_callback(
    chronon_engine* engine, chronon_log_callback callback, void* user);

CHRONON_API chronon_status chronon_plan_compile_json(
    chronon_engine* engine, const char* json, chronon_plan** out_plan);
CHRONON_API chronon_status chronon_plan_compile_json_n(
    chronon_engine* engine, const char* json, uint64_t json_size,
    chronon_plan** out_plan);
CHRONON_API void chronon_plan_destroy(chronon_plan* plan);

CHRONON_API chronon_status chronon_render_frame(
    chronon_engine* engine, const chronon_plan* plan, uint64_t frame,
    chronon_frame_buffer* out_buffer);
CHRONON_API chronon_status chronon_render_frame_into(
    chronon_engine* engine, const chronon_plan* plan, uint64_t frame,
    void* destination, uint64_t destination_size, chronon_frame_info* out_info);
/* ADR-032 motion handshake — render one frame with the motion side's layer
 * states applied between sampling and graph execution.
 *
 * `states` carries `state_count` records; both are ignored when the count is
 * 0, and a count above 0 with a NULL pointer is an invalid argument. Each
 * record decides one named layer's transform, opacity and activity for this
 * frame: the motion side owns WHERE and WHEN, this engine still owns WHAT.
 *
 * A record set that fails the contract's ABI discipline — struct_size smaller
 * than the known record, a reserved (0) or out-of-range layer id, a duplicate
 * layer id, or a non-finite matrix — is refused with CHRONON_ERROR_ABI_MISMATCH
 * before any render work starts, so a malformed handshake can never produce a
 * frame that merely looks plausible. Layer ids that simply address no layer of
 * this composition are tolerated: a composition may draw fewer layers than the
 * motion side tracks, and that is a partial frame, not a bad one. */
CHRONON_API chronon_status chronon_render_frame_with_layers(
    chronon_engine* engine, const chronon_plan* plan, uint64_t frame,
    const chronon_layer_state* states, uint64_t state_count,
    chronon_frame_buffer* out_buffer);
CHRONON_API chronon_status chronon_render_file(
    chronon_engine* engine, const chronon_plan* plan,
    const char* output_path, uint64_t start_frame, uint64_t end_frame,
    uint32_t fps_num, uint32_t fps_den,
    const chronon_render_callbacks* callbacks);
CHRONON_API void chronon_buffer_free(chronon_engine* engine,
                                      chronon_frame_buffer* buffer);

/* Contract-zone version, independent of the engine ABI version.
 * Bumps whenever a contract-zone record or enum changes shape or value.
 * Consumers gate their own struct definitions on this value. */
CHRONON_API uint32_t chronon_contract_version(void);

#ifdef __cplusplus
}
#endif
