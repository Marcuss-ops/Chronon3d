// tests/install_consumer/main_c.c
//
// Standalone C ABI consumer (check_c_api).  Links only libchronon3d_c.so via
// the exported Chronon3D::C target and exercises the frozen C ABI v2 surface
// against the INSTALLED package (never the source tree).
//
// The CABI_01..CABI_13 release-gate matrix:
//   CABI_01  load library                (entry points resolve + version string)
//   CABI_02  chronon_abi_version() == 2
//   CABI_03  engine create / destroy     (NULL-safe destruction)
//   CABI_04  wrong ABI -> ABI_MISMATCH   (structured chronon_error_info)
//   CABI_05  compile minimal RenderPlan
//   CABI_06  compile invalid plan -> structured error
//   CABI_07  render one frame            (engine-owned buffer + buffer_free)
//   CABI_08  caller-owned render_frame_into (size query / too-small / render)
//   CABI_09  render MP4                  (SKIP when video not compiled in)
//   CABI_10  missing asset -> structured error
//   CABI_11  parallel call same engine -> BUSY
//   CABI_12  cancel render               (SKIP when video not compiled in)
//   CABI_13  destroy resources / cleanup safety (leak detection = valgrind)
//
// Each test returns 0 (PASS), 1 (FAIL), or 2 (SKIP).  The driver prints the
// final marker C_ABI_CONSUMER_PASS only when every non-skipped test passed.

#include <chronon3d/c_api/chronon3d.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ── RenderPlan fixtures ────────────────────────────────────────────────────

static const char kPlan[] =
    "{\"schema\":\"chronon.render-plan.v2\",\"version\":2,"
    "\"canvas\":{\"width\":320,\"height\":180,\"fps_num\":30,\"fps_den\":1,\"duration_frames\":2},"
    "\"layers\":[{\"id\":\"bg\",\"type\":\"color\",\"color\":[0.2,0.4,0.6,1.0]}],"
    "\"output\":{\"path\":\"out.png\"}}";

static const char kInvalidPlan[] =
    "{\"schema\":\"chronon.render-plan.v2\",\"version\":2,"
    "\"layers\":[],\"output\":{\"path\":\"out.png\"}}";

static const char kMissingAssetPlan[] =
    "{\"schema\":\"chronon.render-plan.v2\",\"version\":2,"
    "\"canvas\":{\"width\":320,\"height\":180,\"fps_num\":30,\"fps_den\":1,\"duration_frames\":1},"
    "\"layers\":[{\"id\":\"img\",\"type\":\"image\","
    "\"asset\":\"cabi_missing_asset_never_exists.png\"}],"
    "\"output\":{\"path\":\"out.png\"}}";

static const int kExpectedWidth = 320;
static const int kExpectedHeight = 180;

// ── Shared helpers ─────────────────────────────────────────────────────────

// Create an engine with the given assets root (NULL = no mounted root).
// Returns the create_v2 status and, on demand, the structured error info.
static chronon_status make_engine(const char* assets_root,
                                  chronon_engine** out,
                                  chronon_error_info* err) {
    chronon_engine_config config;
    chronon_error_info local;
    memset(&config, 0, sizeof(config));
    memset(&local, 0, sizeof(local));
    config.struct_size = sizeof(config);
    config.abi_version = chronon_abi_version();
    config.assets_root = assets_root;
    local.struct_size = sizeof(local);
    *out = NULL;
    const chronon_status status = chronon_engine_create_v2(&config, out, &local);
    if (err) *err = local;
    return status;
}

// The only reliable runtime signal that video support was not compiled in is
// the render_to_file "Built without CHRONON3D_ENABLE_VIDEO support." message
// surfaced through engine->last_error.  Video tests SKIP on that path.
static int video_unavailable(const chronon_engine* engine) {
    const char* message = chronon_engine_last_error((chronon_engine*)engine);
    return message && strstr(message, "CHRONON3D_ENABLE_VIDEO") != NULL;
}

static int frame_is_nonzero(const uint8_t* pixels, uint64_t size) {
    uint64_t i;
    int nonzero = 0;
    for (i = 0; i < size; ++i) nonzero |= pixels[i] != 0;
    return nonzero;
}

// ── CABI_01 ────────────────────────────────────────────────────────────────
static int test_01_load(void) {
    const char* version = chronon_version_string();
    if (!version || !version[0]) {
        fprintf(stderr, "    chronon_version_string() is empty\n");
        return 1;
    }
    const char* ok = chronon_status_name(CHRONON_OK);
    if (!ok || strcmp(ok, "OK") != 0) {
        fprintf(stderr, "    chronon_status_name(CHRONON_OK) != \"OK\"\n");
        return 1;
    }
    return 0;
}

// ── CABI_02 ────────────────────────────────────────────────────────────────
static int test_02_abi(void) {
    if (chronon_abi_version() != 2) {
        fprintf(stderr, "    chronon_abi_version() == %u, expected 2\n",
                (unsigned)chronon_abi_version());
        return 1;
    }
    return 0;
}

// ── CABI_03 ────────────────────────────────────────────────────────────────
static int test_03_create_destroy(void) {
    chronon_engine* engine = NULL;
    chronon_error_info error;
    if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed: %s\n",
                error.message ? error.message : "no error");
        return 1;
    }
    chronon_engine_destroy(engine);
    chronon_engine_destroy(NULL);  // NULL-safe destruction
    return 0;
}

// ── CABI_04 ────────────────────────────────────────────────────────────────
static int test_04_abi_mismatch(void) {
    chronon_engine_config config;
    chronon_error_info error;
    chronon_engine* engine = (chronon_engine*)1;  // must be reset to NULL
    memset(&config, 0, sizeof(config));
    memset(&error, 0, sizeof(error));
    config.struct_size = sizeof(config);
    config.abi_version = chronon_abi_version() + 1;
    error.struct_size = sizeof(error);

    const chronon_status status =
        chronon_engine_create_v2(&config, &engine, &error);
    if (status != CHRONON_ERROR_ABI_MISMATCH) {
        fprintf(stderr, "    expected ABI_MISMATCH, got %d (%s)\n",
                (int)status, chronon_status_name(status));
        return 1;
    }
    if (engine != NULL) {
        fprintf(stderr, "    engine must stay NULL on ABI_MISMATCH\n");
        return 1;
    }
    if (!error.code || strcmp(error.code, "ABI_MISMATCH") != 0) {
        fprintf(stderr, "    error.code != \"ABI_MISMATCH\"\n");
        return 1;
    }
    if (!error.message || !error.message[0]) {
        fprintf(stderr, "    error.message is empty\n");
        return 1;
    }
    return 0;
}

// ── CABI_05 ────────────────────────────────────────────────────────────────
static int test_05_compile(void) {
    chronon_engine* engine = NULL;
    chronon_plan* plan = NULL;
    chronon_error_info error;
    if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed\n");
        return 1;
    }
    const chronon_status status =
        chronon_plan_compile_json_n(engine, kPlan, sizeof(kPlan) - 1, &plan);
    const int ok = (status == CHRONON_OK && plan != NULL);
    if (!ok) {
        fprintf(stderr, "    compile failed: %d (%s): %s\n", (int)status,
                chronon_status_name(status), chronon_engine_last_error(engine));
    }
    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);
    return ok ? 0 : 1;
}

// ── CABI_06 ────────────────────────────────────────────────────────────────
static int test_06_invalid_plan(void) {
    chronon_engine* engine = NULL;
    chronon_plan* plan = (chronon_plan*)1;
    chronon_error_info error;
    if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed\n");
        return 1;
    }
    const chronon_status status =
        chronon_plan_compile_json_n(engine, kInvalidPlan, sizeof(kInvalidPlan) - 1,
                                    &plan);
    if (status == CHRONON_OK) {
        fprintf(stderr, "    invalid plan was accepted\n");
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        return 1;
    }
    const char* message = chronon_engine_last_error(engine);
    if (!message || !message[0]) {
        fprintf(stderr, "    no structured error message for invalid plan\n");
        chronon_engine_destroy(engine);
        return 1;
    }

    // The engine's last structured error must expose status + code (additive
    // ABI2 surface used by Go/Python bindings instead of string parsing).
    chronon_error_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (chronon_engine_last_error_info(engine, &info) != CHRONON_OK ||
        info.status == CHRONON_OK || !info.code || !info.code[0]) {
        fprintf(stderr, "    last_error_info did not return structured fields\n");
        chronon_engine_destroy(engine);
        return 1;
    }

    chronon_engine_destroy(engine);
    return 0;
}

// ── CABI_07 ────────────────────────────────────────────────────────────────
static int test_07_render_frame(void) {
    chronon_engine* engine = NULL;
    chronon_plan* plan = NULL;
    chronon_error_info error;
    if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed\n");
        return 1;
    }
    if (chronon_plan_compile_json_n(engine, kPlan, sizeof(kPlan) - 1, &plan) !=
            CHRONON_OK ||
        !plan) {
        fprintf(stderr, "    compile failed\n");
        chronon_engine_destroy(engine);
        return 1;
    }

    chronon_frame_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    const chronon_status status = chronon_render_frame(engine, plan, 0, &buffer);
    int ok = (status == CHRONON_OK && buffer.data != NULL && buffer.size > 0 &&
              buffer.width == (uint32_t)kExpectedWidth &&
              buffer.height == (uint32_t)kExpectedHeight);
    if (!ok) {
        fprintf(stderr, "    render frame failed: %d (%s)\n", (int)status,
                chronon_status_name(status));
    } else if (!frame_is_nonzero((const uint8_t*)buffer.data, buffer.size)) {
        fprintf(stderr, "    rendered frame is empty\n");
        ok = 0;
    }
    chronon_buffer_free(engine, &buffer);
    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);
    return ok ? 0 : 1;
}

// ── CABI_08 ────────────────────────────────────────────────────────────────
static int test_08_render_frame_into(void) {
    chronon_engine* engine = NULL;
    chronon_plan* plan = NULL;
    chronon_error_info error;
    if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed\n");
        return 1;
    }
    if (chronon_plan_compile_json_n(engine, kPlan, sizeof(kPlan) - 1, &plan) !=
            CHRONON_OK ||
        !plan) {
        fprintf(stderr, "    compile failed\n");
        chronon_engine_destroy(engine);
        return 1;
    }

    int ok = 1;
    chronon_frame_info info;
    memset(&info, 0, sizeof(info));

    // 1. Size query: NULL destination reports the required byte count.
    chronon_status status = chronon_render_frame_into(engine, plan, 0, NULL, 0, &info);
    if (status != CHRONON_ERROR_BUFFER_TOO_SMALL || info.size == 0 ||
        info.width != (uint32_t)kExpectedWidth ||
        info.height != (uint32_t)kExpectedHeight) {
        fprintf(stderr, "    size query failed: %d (%s)\n", (int)status,
                chronon_status_name(status));
        ok = 0;
    }

    // 2. Too-small destination must fail and keep reporting the full size.
    if (ok && info.size > 1) {
        uint8_t* small = (uint8_t*)malloc((size_t)(info.size - 1));
        if (!small) {
            fprintf(stderr, "    out of memory\n");
            ok = 0;
        } else {
            chronon_frame_info small_info;
            memset(&small_info, 0, sizeof(small_info));
            status = chronon_render_frame_into(engine, plan, 0, small,
                                               info.size - 1, &small_info);
            if (status != CHRONON_ERROR_BUFFER_TOO_SMALL ||
                small_info.size != info.size) {
                fprintf(stderr, "    too-small buffer not rejected correctly: %d\n",
                        (int)status);
                ok = 0;
            }
            free(small);
        }
    }

    // 3. Exact-size destination renders successfully with non-empty pixels.
    if (ok) {
        uint8_t* pixels = (uint8_t*)malloc((size_t)info.size);
        if (!pixels) {
            fprintf(stderr, "    out of memory\n");
            ok = 0;
        } else {
            status = chronon_render_frame_into(engine, plan, 0, pixels,
                                               info.size, &info);
            if (status != CHRONON_OK) {
                fprintf(stderr, "    render into failed: %d (%s)\n", (int)status,
                        chronon_status_name(status));
                ok = 0;
            } else if (!frame_is_nonzero(pixels, info.size)) {
                fprintf(stderr, "    rendered frame is empty\n");
                ok = 0;
            }
            free(pixels);
        }
    }

    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);
    return ok ? 0 : 1;
}

// ── CABI_09 ────────────────────────────────────────────────────────────────
static int test_09_render_mp4(void) {
    const char* path = "chronon3d_cabi_c09.mp4";
    remove(path);

    chronon_engine* engine = NULL;
    chronon_plan* plan = NULL;
    chronon_error_info error;
    if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed\n");
        return 1;
    }
    if (chronon_plan_compile_json_n(engine, kPlan, sizeof(kPlan) - 1, &plan) !=
            CHRONON_OK ||
        !plan) {
        fprintf(stderr, "    compile failed\n");
        chronon_engine_destroy(engine);
        return 1;
    }

    const chronon_status status =
        chronon_render_file(engine, plan, path, 0, 1, 30, 1, NULL);

    int skip = 0;
    long size = -1;
    if (status == CHRONON_OK) {
        FILE* file = fopen(path, "rb");
        if (!file) {
            fprintf(stderr, "    output file missing after render_file\n");
        } else {
            fseek(file, 0, SEEK_END);
            size = ftell(file);
            fclose(file);
        }
    } else if (video_unavailable(engine)) {
        skip = 1;
    } else {
        fprintf(stderr, "    render_file failed: %d (%s): %s\n", (int)status,
                chronon_status_name(status), chronon_engine_last_error(engine));
    }

    remove(path);
    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);

    if (skip) return 2;
    if (status != CHRONON_OK) return 1;
    if (size <= 0) {
        fprintf(stderr, "    output file is empty\n");
        return 1;
    }
    return 0;
}

// ── CABI_10 ────────────────────────────────────────────────────────────────
// A missing image asset is rejected during plan compilation with the specific
// CHRONON_ERROR_ASSET_NOT_FOUND status (not the generic PARSE_FAILED), plus a
// structured last error exposing the offending asset path.
static int test_10_missing_asset(void) {
    chronon_engine* engine = NULL;
    chronon_plan* plan = (chronon_plan*)1;
    chronon_error_info error;
    // AssetResolver::mount() requires an ABSOLUTE root (throws on relative
    // paths), so mount the current working directory in absolute form.  The
    // fixture asset still never exists, so the missing-asset path is the one
    // under test.
    char assets_root[4096];
    if (!getcwd(assets_root, sizeof(assets_root))) {
        fprintf(stderr, "    getcwd failed\n");
        return 1;
    }
    if (make_engine(assets_root, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed\n");
        return 1;
    }
    const chronon_status status = chronon_plan_compile_json_n(
        engine, kMissingAssetPlan, sizeof(kMissingAssetPlan) - 1, &plan);
    if (status != CHRONON_ERROR_ASSET_NOT_FOUND) {
        fprintf(stderr, "    expected ASSET_NOT_FOUND, got %d (%s)\n",
                (int)status, chronon_status_name(status));
        chronon_engine_destroy(engine);
        return 1;
    }
    const char* message = chronon_engine_last_error(engine);
    int ok = (message != NULL && message[0] != '\0');
    if (!ok) {
        fprintf(stderr, "    no structured error message for missing asset\n");
    }

    // The structured last error must expose the ASSET_NOT_FOUND code + the
    // offending asset path + the asset_resolver component.
    chronon_error_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (chronon_engine_last_error_info(engine, &info) != CHRONON_OK ||
        info.status != CHRONON_ERROR_ASSET_NOT_FOUND ||
        !info.code || strcmp(info.code, "ASSET_NOT_FOUND") != 0 ||
        !info.asset || strcmp(info.asset, "cabi_missing_asset_never_exists.png") != 0 ||
        !info.component || strcmp(info.component, "asset_resolver") != 0) {
        fprintf(stderr, "    last_error_info did not expose the structured ASSET_NOT_FOUND fields\n");
        ok = 0;
    }

    chronon_engine_destroy(engine);
    return ok ? 0 : 1;
}

// ── CABI_11 ────────────────────────────────────────────────────────────────

struct busy_thread_arg {
    chronon_engine* engine;
    chronon_plan* plan;
    uint8_t* pixels;
    uint64_t size;
    int frame;
    chronon_status status;
};

// Release gate: every thread spins until the main thread flips this to 1, so
// the concurrent render calls all begin in the same instant and at least one
// overlap (guaranteeing a BUSY observation on the slow busy plan).
static atomic_int g_start_gate = 0;

static void* busy_thread_main(void* user) {
    struct busy_thread_arg* arg = (struct busy_thread_arg*)user;
    chronon_frame_info info;
    memset(&info, 0, sizeof(info));
    while (atomic_load_explicit(&g_start_gate, memory_order_acquire) == 0) {
    }
    arg->status = chronon_render_frame_into(
        arg->engine, arg->plan, (uint64_t)arg->frame, arg->pixels, arg->size,
        &info);
    return NULL;
}

static size_t build_busy_plan(char* buffer, size_t capacity, int layer_count) {
    int written = snprintf(buffer, capacity,
        "{\"schema\":\"chronon.render-plan.v2\",\"version\":2,"
        "\"canvas\":{\"width\":1920,\"height\":1080,\"fps_num\":30,\"fps_den\":1,"
        "\"duration_frames\":2},\"layers\":[");
    int i;
    for (i = 0; i < layer_count; ++i) {
        written += snprintf(buffer + written, capacity - (size_t)written,
            "%s{\"id\":\"busy%d\",\"type\":\"color\",\"color\":[0.2,0.4,0.6,1.0]}",
            i ? "," : "", i);
    }
    written += snprintf(buffer + written, capacity - (size_t)written,
        "],\"output\":{\"path\":\"out.png\"}}");
    return (size_t)written;
}

static int test_11_busy(void) {
    char plan_json[32768];
    const size_t plan_len = build_busy_plan(plan_json, sizeof(plan_json), 64);

    chronon_engine* engine = NULL;
    chronon_plan* plan = NULL;
    chronon_error_info error;
    if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed\n");
        return 1;
    }
    if (chronon_plan_compile_json_n(engine, plan_json, plan_len, &plan) !=
            CHRONON_OK ||
        !plan) {
        fprintf(stderr, "    busy plan compile failed\n");
        chronon_engine_destroy(engine);
        return 1;
    }

    chronon_frame_info info;
    memset(&info, 0, sizeof(info));
    if (chronon_render_frame_into(engine, plan, 0, NULL, 0, &info) !=
            CHRONON_ERROR_BUFFER_TOO_SMALL ||
        info.size == 0) {
        fprintf(stderr, "    busy plan size query failed\n");
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        return 1;
    }

    uint8_t* pixels = (uint8_t*)malloc((size_t)info.size);
    if (!pixels) {
        fprintf(stderr, "    out of memory\n");
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        return 1;
    }

    const int kThreads = 8;
    pthread_t threads[kThreads];
    struct busy_thread_arg args[kThreads];
    int i;
    atomic_store_explicit(&g_start_gate, 0, memory_order_release);
    for (i = 0; i < kThreads; ++i) {
        args[i].engine = engine;
        args[i].plan = plan;
        args[i].pixels = pixels;
        args[i].size = info.size;
        args[i].frame = i % 2;
        args[i].status = CHRONON_ERROR_UNKNOWN;
        pthread_create(&threads[i], NULL, busy_thread_main, &args[i]);
    }
    atomic_store_explicit(&g_start_gate, 1, memory_order_release);
    for (i = 0; i < kThreads; ++i) pthread_join(threads[i], NULL);

    int busy = 0;
    int success = 0;
    for (i = 0; i < kThreads; ++i) {
        if (args[i].status == CHRONON_ERROR_BUSY) ++busy;
        else if (args[i].status == CHRONON_OK) ++success;
    }

    free(pixels);
    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);

    if (busy == 0) {
        fprintf(stderr, "    no BUSY status observed (%d ok, 0 busy)\n", success);
        return 1;
    }
    if (success == 0) {
        fprintf(stderr, "    no successful render observed\n");
        return 1;
    }
    return 0;
}

// ── CABI_12 ────────────────────────────────────────────────────────────────
static int always_cancel(void* user) {
    (void)user;
    return 1;
}

static int test_12_cancel(void) {
    const char* path = "chronon3d_cabi_c12.mp4";
    remove(path);

    chronon_engine* engine = NULL;
    chronon_plan* plan = NULL;
    chronon_error_info error;
    if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
        fprintf(stderr, "    engine create failed\n");
        return 1;
    }
    if (chronon_plan_compile_json_n(engine, kPlan, sizeof(kPlan) - 1, &plan) !=
            CHRONON_OK ||
        !plan) {
        fprintf(stderr, "    compile failed\n");
        chronon_engine_destroy(engine);
        return 1;
    }

    chronon_render_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.is_cancelled = always_cancel;

    const chronon_status status =
        chronon_render_file(engine, plan, path, 0, 1, 30, 1, &callbacks);

    int skip = 0;
    int ok = 1;
    if (status == CHRONON_ERROR_CANCELLED) {
        FILE* file = fopen(path, "rb");
        if (file) {
            fclose(file);
            fprintf(stderr, "    cancelled render left an output file behind\n");
            ok = 0;
        }
    } else if (video_unavailable(engine)) {
        skip = 1;
    } else {
        fprintf(stderr, "    expected CANCELLED, got %d (%s)\n", (int)status,
                chronon_status_name(status));
        ok = 0;
    }

    remove(path);
    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);

    if (skip) return 2;
    return ok ? 0 : 1;
}

// ── CABI_13 ────────────────────────────────────────────────────────────────
// Deterministic destruction/cleanup safety: repeated create→compile→render→
// destroy cycles plus NULL-safe destruction.  True leak detection is a
// valgrind / AddressSanitizer forward-point, out of scope for this consumer.
static int test_13_destroy(void) {
    int cycle;
    for (cycle = 0; cycle < 8; ++cycle) {
        chronon_engine* engine = NULL;
        chronon_plan* plan = NULL;
        chronon_error_info error;
        if (make_engine(NULL, &engine, &error) != CHRONON_OK || !engine) {
            fprintf(stderr, "    engine create failed (cycle %d)\n", cycle);
            return 1;
        }
        if (chronon_plan_compile_json_n(engine, kPlan, sizeof(kPlan) - 1, &plan) !=
                CHRONON_OK ||
            !plan) {
            fprintf(stderr, "    compile failed (cycle %d)\n", cycle);
            chronon_engine_destroy(engine);
            return 1;
        }
        chronon_frame_info info;
        memset(&info, 0, sizeof(info));
        chronon_status status =
            chronon_render_frame_into(engine, plan, 0, NULL, 0, &info);
        if (status != CHRONON_ERROR_BUFFER_TOO_SMALL || info.size == 0) {
            fprintf(stderr, "    size query failed (cycle %d)\n", cycle);
            chronon_plan_destroy(plan);
            chronon_engine_destroy(engine);
            return 1;
        }
        uint8_t* pixels = (uint8_t*)malloc((size_t)info.size);
        if (!pixels) {
            fprintf(stderr, "    out of memory (cycle %d)\n", cycle);
            chronon_plan_destroy(plan);
            chronon_engine_destroy(engine);
            return 1;
        }
        status = chronon_render_frame_into(engine, plan, 0, pixels, info.size,
                                           &info);
        free(pixels);
        if (status != CHRONON_OK) {
            fprintf(stderr, "    render failed (cycle %d): %d\n", cycle,
                    (int)status);
            chronon_plan_destroy(plan);
            chronon_engine_destroy(engine);
            return 1;
        }
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
    }
    chronon_plan_destroy(NULL);
    chronon_engine_destroy(NULL);
    return 0;
}

// ── Driver ─────────────────────────────────────────────────────────────────

struct cabi_test {
    const char* id;
    int (*fn)(void);
};

static const struct cabi_test kTests[] = {
    {"CABI_01", test_01_load},
    {"CABI_02", test_02_abi},
    {"CABI_03", test_03_create_destroy},
    {"CABI_04", test_04_abi_mismatch},
    {"CABI_05", test_05_compile},
    {"CABI_06", test_06_invalid_plan},
    {"CABI_07", test_07_render_frame},
    {"CABI_08", test_08_render_frame_into},
    {"CABI_09", test_09_render_mp4},
    {"CABI_10", test_10_missing_asset},
    {"CABI_11", test_11_busy},
    {"CABI_12", test_12_cancel},
    {"CABI_13", test_13_destroy},
};

int main(void) {
    int pass = 0;
    int skip = 0;
    int fail = 0;
    size_t i;

    puts("=== Chronon3D C ABI consumer (CABI_01..CABI_13) ===");
    for (i = 0; i < sizeof(kTests) / sizeof(kTests[0]); ++i) {
        fflush(stdout);
        const int rc = kTests[i].fn();
        switch (rc) {
            case 0: printf("[%s] PASS\n", kTests[i].id); ++pass; break;
            case 2: printf("[%s] SKIP\n", kTests[i].id); ++skip; break;
            default: printf("[%s] FAIL\n", kTests[i].id); ++fail; break;
        }
    }

    printf("CABI: %d pass, %d skip, %d fail\n", pass, skip, fail);
    if (fail != 0) {
        printf("C_ABI_CONSUMER_FAIL\n");
        return 1;
    }
    printf("C_ABI_CONSUMER_PASS\n");
    return 0;
}
