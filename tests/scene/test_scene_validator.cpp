#include <doctest/doctest.h>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/validation/scene_validator.hpp>
using namespace chronon3d;


// ── Helpers ──────────────────────────────────────────────────────────────────

static Layer make_layer(const char* name, Frame duration = 60) {
    Layer l;
    l.name = name;
    l.duration = duration;
    return l;
}

static Layer make_null_layer(const char* name) {
    Layer l;
    l.name = name;
    l.kind = LayerKind::Null;
    l.duration = 0; // Null layers with zero duration are acceptable
    return l;
}

// ── Clean scene (no diagnostics) ─────────────────────────────────────────────

TEST_CASE("SceneValidator: clean scene produces no diagnostics") {
    Scene scene;
    scene.add_layer(make_layer("bg"));
    scene.add_layer(make_layer("text"));
    scene.add_layer(make_layer("icon"));

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK_FALSE(result.has_any());
    CHECK_FALSE(result.has_errors());
    CHECK_FALSE(result.has_warnings());
    CHECK(result.diagnostics.empty());
}

// ── Rule 1: layer.duplicate_name ──────────────────────────────────────────────

TEST_CASE("SceneValidator: layer.duplicate_name detects identical names") {
    Scene scene;
    scene.add_layer(make_layer("card"));
    scene.add_layer(make_layer("bg"));
    scene.add_layer(make_layer("card")); // duplicate

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_errors());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.duplicate_name" && d.context == "card") {
            found = true;
            CHECK(d.severity == ValidationSeverity::Error);
        }
    }
    CHECK(found);
}

TEST_CASE("SceneValidator: layer.duplicate_name triggers once per duplicate group") {
    Scene scene;
    scene.add_layer(make_layer("x"));
    scene.add_layer(make_layer("x")); // 1st duplicate
    scene.add_layer(make_layer("x")); // 2nd duplicate

    SceneValidator validator;
    auto result = validator.validate(scene);

    int count = 0;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.duplicate_name") ++count;
    }
    // Three layers with same name → 2 duplicates reported
    CHECK(count == 2);
}

// ── Rule 2: layer.missing_parent ─────────────────────────────────────────────

TEST_CASE("SceneValidator: layer.missing_parent detects non-existent parent") {
    Scene scene;
    scene.add_layer(make_layer("child"));
    scene.layers().back().parent_name = "ghost_parent";

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_warnings());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.missing_parent" && d.context == "child") {
            found = true;
            CHECK(d.severity == ValidationSeverity::Warning);
        }
    }
    CHECK(found);
}

TEST_CASE("SceneValidator: layer.missing_parent passes when parent exists") {
    Scene scene;
    scene.add_layer(make_layer("parent"));
    scene.add_layer(make_layer("child"));
    scene.layers().back().parent_name = "parent";

    SceneValidator validator;
    auto result = validator.validate(scene);

    for (const auto& d : result.diagnostics) {
        CHECK(d.rule_id != "layer.missing_parent");
    }
}

// ── Rule 3: layer.zero_duration ───────────────────────────────────────────────

TEST_CASE("SceneValidator: layer.zero_duration detects zero-duration normal layer") {
    Scene scene;
    scene.add_layer(make_layer("bg", 100));
    scene.add_layer(make_layer("ghost", 0)); // zero duration

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_warnings());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.zero_duration" && d.context == "ghost") {
            found = true;
            CHECK(d.severity == ValidationSeverity::Warning);
        }
    }
    CHECK(found);
}

TEST_CASE("SceneValidator: layer.zero_duration ignores Null layers") {
    Scene scene;
    scene.add_layer(make_null_layer("null_parent"));

    SceneValidator validator;
    auto result = validator.validate(scene);

    for (const auto& d : result.diagnostics) {
        CHECK(d.rule_id != "layer.zero_duration");
    }
}

// ── Rule 4: layer.track_matte_missing_source ─────────────────────────────────

TEST_CASE("SceneValidator: track_matte_missing_source detects missing source layer") {
    Scene scene;
    scene.add_layer(make_layer("fg"));

    Layer matte_layer;
    matte_layer.name = "matte";
    matte_layer.duration = 60;
    matte_layer.track_matte.type = TrackMatteType::Alpha;
    matte_layer.track_matte.source_layer = "nonexistent_source";
    scene.add_layer(std::move(matte_layer));

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_errors());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.track_matte_missing_source" && d.context == "matte") {
            found = true;
            CHECK(d.severity == ValidationSeverity::Error);
        }
    }
    CHECK(found);
}

TEST_CASE("SceneValidator: track_matte_missing_source passes when source exists") {
    Scene scene;
    scene.add_layer(make_layer("alpha_source"));

    Layer matte_layer;
    matte_layer.name = "matte";
    matte_layer.duration = 60;
    matte_layer.track_matte.type = TrackMatteType::Alpha;
    matte_layer.track_matte.source_layer = "alpha_source";
    scene.add_layer(std::move(matte_layer));

    SceneValidator validator;
    auto result = validator.validate(scene);

    for (const auto& d : result.diagnostics) {
        CHECK(d.rule_id != "layer.track_matte_missing_source");
    }
}

// ── Rule 5: camera.missing_target ────────────────────────────────────────────

TEST_CASE("SceneValidator: camera.missing_target detects non-existent target") {
    Scene scene;
    scene.add_layer(make_layer("bg"));

    Camera2_5D cam;
    cam.enabled = true;
    cam.target_name = "nonexistent_target";
    scene.set_camera_2_5d(cam);

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_warnings());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "camera.missing_target") {
            found = true;
            CHECK(d.severity == ValidationSeverity::Warning);
        }
    }
    CHECK(found);
}

TEST_CASE("SceneValidator: camera.missing_target passes when target exists") {
    Scene scene;
    scene.add_layer(make_layer("hero_card"));

    Camera2_5D cam;
    cam.enabled = true;
    cam.target_name = "hero_card";
    scene.set_camera_2_5d(cam);

    SceneValidator validator;
    auto result = validator.validate(scene);

    for (const auto& d : result.diagnostics) {
        CHECK(d.rule_id != "camera.missing_target");
    }
}

TEST_CASE("SceneValidator: camera.missing_target skipped when camera disabled") {
    Scene scene;
    scene.add_layer(make_layer("bg"));

    Camera2_5D cam;
    cam.enabled = false;
    cam.target_name = "nonexistent";
    scene.set_camera_2_5d(cam);

    SceneValidator validator;
    auto result = validator.validate(scene);

    for (const auto& d : result.diagnostics) {
        CHECK(d.rule_id != "camera.missing_target");
    }
}

// ── Rule 6: layer.circular_parent ─────────────────────────────────────────────

TEST_CASE("SceneValidator: layer.circular_parent detects A→A self-cycle") {
    Scene scene;
    scene.add_layer(make_layer("self_ref"));
    scene.layers().back().parent_name = "self_ref";

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_errors());

    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.circular_parent" && d.context == "self_ref") {
            found = true;
            CHECK(d.severity == ValidationSeverity::Error);
        }
    }
    CHECK(found);
}

TEST_CASE("SceneValidator: layer.circular_parent detects A→B→A cycle") {
    Scene scene;
    scene.add_layer(make_layer("layer_a"));
    scene.layers().back().parent_name = "layer_b";

    scene.add_layer(make_layer("layer_b"));
    scene.layers().back().parent_name = "layer_a";

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_errors());

    bool found_a = false;
    bool found_b = false;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.circular_parent") {
            if (d.context == "layer_a") found_a = true;
            if (d.context == "layer_b") found_b = true;
        }
    }
    CHECK(found_a);
    CHECK(found_b);
}

TEST_CASE("SceneValidator: layer.circular_parent detects A→B→C→A chain") {
    Scene scene;
    scene.add_layer(make_layer("node_a"));
    scene.layers().back().parent_name = "node_c";

    scene.add_layer(make_layer("node_b"));
    scene.layers().back().parent_name = "node_a";

    scene.add_layer(make_layer("node_c"));
    scene.layers().back().parent_name = "node_b";

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_errors());

    int cycle_count = 0;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.circular_parent") ++cycle_count;
    }
    // All 3 layers in the cycle should be reported
    CHECK(cycle_count == 3);
}

TEST_CASE("SceneValidator: layer.circular_parent passes for valid hierarchy") {
    Scene scene;
    scene.add_layer(make_layer("root"));
    scene.add_layer(make_layer("child"));
    scene.layers().back().parent_name = "root";
    scene.add_layer(make_layer("grandchild"));
    scene.layers().back().parent_name = "child";

    SceneValidator validator;
    auto result = validator.validate(scene);

    for (const auto& d : result.diagnostics) {
        CHECK(d.rule_id != "layer.circular_parent");
    }
}

// ── Multiple simultaneous errors ─────────────────────────────────────────────

TEST_CASE("SceneValidator: catches multiple different errors at once") {
    Scene scene;
    scene.add_layer(make_layer("dup"));  // will be duplicated
    scene.add_layer(make_layer("dup"));  // duplicate name
    scene.add_layer(make_layer("orphan", 0)); // zero duration
    scene.layers().back().parent_name = "nonexistent_parent"; // missing parent

    SceneValidator validator;
    auto result = validator.validate(scene);

    CHECK(result.has_any());

    bool has_dup = false, has_parent = false, has_duration = false;
    for (const auto& d : result.diagnostics) {
        if (d.rule_id == "layer.duplicate_name") has_dup = true;
        if (d.rule_id == "layer.missing_parent") has_parent = true;
        if (d.rule_id == "layer.zero_duration") has_duration = true;
    }
    CHECK(has_dup);
    CHECK(has_parent);
    CHECK(has_duration);
}

// ── to_text() ────────────────────────────────────────────────────────────────

TEST_CASE("SceneValidator: to_text() produces readable output") {
    Scene scene;
    scene.add_layer(make_layer("a"));
    scene.add_layer(make_layer("a")); // duplicate

    SceneValidator validator;
    auto result = validator.validate(scene);

    std::string text = result.to_text();
    CHECK_FALSE(text.empty());
    CHECK(text.find("layer.duplicate_name") != std::string::npos);
    CHECK(text.find("ERROR") != std::string::npos);
}

// ── errors() and warnings() filtering ────────────────────────────────────────

TEST_CASE("SceneValidator: errors() and warnings() filter correctly") {
    Scene scene;
    // Duplicate name → Error
    scene.add_layer(make_layer("x"));
    scene.add_layer(make_layer("x"));

    // Missing parent → Warning
    scene.add_layer(make_layer("child"));
    scene.layers().back().parent_name = "missing";

    SceneValidator validator;
    auto result = validator.validate(scene);

    auto errs = result.errors();
    auto warns = result.warnings();

    for (const auto& e : errs) {
        CHECK(e.severity == ValidationSeverity::Error);
    }
    for (const auto& w : warns) {
        CHECK(w.severity == ValidationSeverity::Warning);
    }

    CHECK_FALSE(errs.empty());
    CHECK_FALSE(warns.empty());
}
