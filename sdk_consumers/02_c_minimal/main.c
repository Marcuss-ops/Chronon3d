// sdk_consumers/02_c_minimal/main.c
//
// Minimal C ABI consumer.  Links only libchronon3d_c.so (the header is the
// single public C surface).  Compiles the canonical RenderPlan JSON, renders
// one frame into a caller-owned buffer, and asserts a non-empty frame.

#include <chronon3d/c_api/chronon3d.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(const char* step, chronon_status status) {
    fprintf(stderr, "[c-minimal] %s failed: status=%d (%s)\n", step,
            (int)status, chronon_status_name(status));
    return 1;
}

int main(void) {
    FILE* file = fopen("plan.json", "rb");
    if (!file) {
        fprintf(stderr, "cannot open plan.json\n");
        return 1;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    char* json = (char*)malloc((size_t)length + 1);
    if (!json || fread(json, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "failed to read plan.json\n");
        free(json);
        fclose(file);
        return 1;
    }
    json[length] = '\0';
    fclose(file);

    chronon_engine_config config = {sizeof(config), chronon_abi_version(), NULL, 0};
    chronon_engine* engine = NULL;
    chronon_error_info error = {sizeof(error), CHRONON_OK, NULL, NULL, NULL, NULL, NULL};
    if (chronon_engine_create_v2(&config, &engine, &error) != CHRONON_OK ||
        !engine) {
        fprintf(stderr, "engine create failed: %s\n",
                error.message ? error.message : "no error");
        free(json);
        return 1;
    }

    chronon_plan* plan = NULL;
    chronon_status status =
        chronon_plan_compile_json_n(engine, json, (uint64_t)length, &plan);
    if (status != CHRONON_OK || !plan) {
        int result = fail("compile", status);
        chronon_engine_destroy(engine);
        free(json);
        return result;
    }

    chronon_frame_info info = {0, 0, 0, 0, 0};
    status = chronon_render_frame_into(engine, plan, 0, NULL, 0, &info);
    if (status != CHRONON_ERROR_BUFFER_TOO_SMALL || info.size == 0) {
        int result = fail("size query", status);
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        free(json);
        return result;
    }

    uint8_t* pixels = (uint8_t*)malloc((size_t)info.size);
    if (!pixels) {
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        free(json);
        return 1;
    }
    status = chronon_render_frame_into(engine, plan, 0, pixels, info.size, &info);
    if (status != CHRONON_OK) {
        int result = fail("render frame", status);
        free(pixels);
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        free(json);
        return result;
    }

    int nonzero = 0;
    for (uint64_t i = 0; i < info.size; ++i) {
        nonzero |= pixels[i] != 0;
    }
    free(pixels);
    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);
    free(json);

    if (!nonzero) {
        fprintf(stderr, "rendered frame is empty\n");
        return 1;
    }
    printf("C_ABI_CONSUMER_PASS %ux%u\n", info.width, info.height);
    return 0;
}
