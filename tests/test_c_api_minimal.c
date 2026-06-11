#include "chronon3d/c_api/chronon3d.h"
#include <stdio.h>

int main() {
    printf("Chronon3D C API Minimal Test\n");
    printf("Version: %s\n", chronon_version_string());

    chronon_context* ctx = chronon_create_context();
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    chronon_destroy_context(ctx);

    printf("Minimal C API test passed.\n");
    return 0;
}
