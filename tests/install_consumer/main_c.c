#include <chronon3d/c_api/chronon3d.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_status(const char* step, chronon_status actual) {
    fprintf(stderr, "[C-ABI-FAIL] %s: status=%d\n", step, (int)actual);
    return 1;
}

int main(void) {
    static const char json[] =
        "{\"schema\":\"chronon.render-plan\",\"version\":1,"
        "\"canvas\":{\"width\":2,\"height\":2,\"fps\":30,"
        "\"duration_frames\":2},\"layers\":[{\"id\":\"bg\","
        "\"type\":\"color\",\"color\":[0.2,0.4,0.6,1.0]}],"
        "\"output\":{\"path\":\"out.png\"}}";

    chronon_engine_config config = {
        sizeof(config), chronon_abi_version(), NULL, 0};
    chronon_engine* engine = NULL;
    chronon_error_info error = {sizeof(error), CHRONON_OK, NULL};
    if (chronon_engine_create_v2(&config, &engine, &error) != CHRONON_OK ||
        !engine) {
        fprintf(stderr, "[C-ABI-FAIL] create: %s\n",
                error.message ? error.message : "no error");
        return 1;
    }

    chronon_plan* plan = NULL;
    chronon_status status = chronon_plan_compile_json_n(
        engine, json, (uint64_t)(sizeof(json) - 1), &plan);
    if (status != CHRONON_OK || !plan) {
        int result = fail_status("compile", status);
        chronon_engine_destroy(engine);
        return result;
    }

    chronon_frame_info info = {0, 0, 0, 0, 0};
    status = chronon_render_frame_into(engine, plan, 0, NULL, 0, &info);
    if (status != CHRONON_ERROR_BUFFER_TOO_SMALL || info.size == 0) {
        int result = fail_status("size query", status);
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        return result;
    }

    uint8_t* pixels = (uint8_t*)malloc((size_t)info.size);
    if (!pixels) return 1;
    status = chronon_render_frame_into(engine, plan, 0, pixels, info.size, &info);
    if (status != CHRONON_OK || info.width != 2 || info.height != 2) {
        int result = fail_status("render frame", status);
        free(pixels);
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        return result;
    }

    int nonzero = 0;
    for (uint64_t i = 0; i < info.size; ++i) nonzero |= pixels[i] != 0;
    free(pixels);
    if (!nonzero) {
        fprintf(stderr, "[C-ABI-FAIL] rendered frame is empty\n");
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        return 1;
    }

    status = chronon_render_frame_into(engine, plan, 1, NULL, 0, &info);
    if (status != CHRONON_ERROR_BUFFER_TOO_SMALL || info.size == 0) {
        int result = fail_status("engine reuse", status);
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        return result;
    }

    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);
    puts("C_ABI_CONSUMER_PASS");
    return 0;
}
