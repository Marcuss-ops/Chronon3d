#include "chronon3d/c_api/chronon3d.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Chronon3D C API Test starting...\n");
    printf("Version: %s\n", chronon_version_string());

    chronon_context* ctx = chronon_create_context();
    if (!ctx) {
        fprintf(stderr, "Failed to create chronon context\n");
        return 1;
    }

    chronon_render_options options = {0};
    options.width = 1920;
    options.height = 1080;
    options.fps = 30;

    // Render 1: GridColorShowcase (Frame 0)
    options.frame = 0;
    printf("Rendering GridColorShowcase to output/grid_color.png...\n");
    chronon_status status = chronon_render_json_file(
        ctx,
        "GridColorShowcase",
        "output/grid_color.png",
        &options
    );

    if (status != CHRONON_OK) {
        fprintf(stderr, "Render 1 failed: %s\n", chronon_last_error(ctx));
        chronon_destroy_context(ctx);
        return 1;
    }
    printf("Render 1 succeeded! Saved to output/grid_color.png\n");

    // Render 2: GlowOrbGalaxy (Frame 30)
    options.frame = 30;
    printf("Rendering GlowOrbGalaxy to output/glow_orb_galaxy.png...\n");
    status = chronon_render_json_file(
        ctx,
        "GlowOrbGalaxy",
        "output/glow_orb_galaxy.png",
        &options
    );

    if (status != CHRONON_OK) {
        fprintf(stderr, "Render 2 failed: %s\n", chronon_last_error(ctx));
        chronon_destroy_context(ctx);
        return 1;
    }
    printf("Render 2 succeeded! Saved to output/glow_orb_galaxy.png\n");

    chronon_destroy_context(ctx);
    return 0;
}
