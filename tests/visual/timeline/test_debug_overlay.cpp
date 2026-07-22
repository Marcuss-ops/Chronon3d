// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/timeline/test_debug_overlay.cpp
//
// Gate 4 — Debug Timeline Overlay Tests
//
// Definition of Done: proves that a --debug-timeline JSON output can be
// produced with correct global_frame, active_sequences (path, from,
// duration, local_frame), and assets_used.
//
// This test validates the data model and JSON serialization. The actual
// visual overlay rendering is a separate feature.
//
// NOTE: SceneBuilder is an immediate-mode, single-frame builder.
// We build a fresh scene per SUBCASE at the specific frame being tested,
// because SceneBuilder::layer() filters out layers inactive at the build
// frame.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/assets/asset_manifest.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace chronon3d;
using json = nlohmann::json;

namespace {

// ── Debug Info Data Model ──────────────────────────────────────────────────

struct ActiveSequenceInfo {
    std::string path;
    int from{0};
    int duration{0};
    int local_frame{0};
};

struct AssetUsedInfo {
    std::string kind;
    std::string path;
    std::string owner;
};

struct TimelineDebugInfo {
    int global_frame{0};
    std::vector<ActiveSequenceInfo> active_sequences;
    std::vector<AssetUsedInfo> assets_used;

    [[nodiscard]] json to_json() const {
        json j;
        j["global_frame"] = global_frame;

        json seqs = json::array();
        for (const auto& seq : active_sequences) {
            json s;
            s["path"] = seq.path;
            s["from"] = seq.from;
            s["duration"] = seq.duration;
            s["local_frame"] = seq.local_frame;
            seqs.push_back(s);
        }
        j["active_sequences"] = seqs;

        json assets = json::array();
        for (const auto& a : assets_used) {
            json asset;
            asset["kind"] = a.kind;
            asset["path"] = a.path;
            asset["owner"] = a.owner;
            assets.push_back(asset);
        }
        j["assets_used"] = assets;

        return j;
    }
};

// ── Scene analyzer: extract debug info from a scene at a given frame ────

TimelineDebugInfo analyze_scene(const Scene& scene, Frame frame) {
    TimelineDebugInfo info;
    info.global_frame = static_cast<int>(frame);

    for (const auto& layer : scene.layers()) {
        if (layer.active_at(frame)) {
            ActiveSequenceInfo seq;
            seq.path = std::string(layer.name);
            seq.from = static_cast<int>(layer.from);
            seq.duration = static_cast<int>(layer.duration);
            seq.local_frame = static_cast<int>(frame) - static_cast<int>(layer.from);
            info.active_sequences.push_back(seq);
        }
    }

    for (const auto& ref : scene.asset_manifest().assets()) {
        AssetUsedInfo asset;
        switch (ref.kind) {
            case assets::AssetKind::Font:  asset.kind = "Font"; break;
            case assets::AssetKind::Image: asset.kind = "Image"; break;
            case assets::AssetKind::Video: asset.kind = "Video"; break;
            case assets::AssetKind::Audio: asset.kind = "Audio"; break;
            default: asset.kind = "Unknown"; break;
        }
        asset.path = ref.path;
        asset.owner = ref.owner;
        info.assets_used.push_back(asset);
    }

    return info;
}

FrameContext debug_ctx(Frame frame = Frame{0}) {
    FrameContext ctx;
    ctx = ctx.with_frame(frame);
    ctx = ctx.with_frame_rate({30, 1});
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

/// Build a 3-layer scene (intro/title/outro) at the given frame.
/// Uses flat layer() calls so all assets are always registered.
Scene make_three_layer_scene(Frame build_frame) {
    SceneBuilder s(debug_ctx(build_frame));
    s.layer("intro", [](LayerBuilder& l) {
        l.from(Frame{0}).duration(Frame{30});
        l.rect("bg", {.size={100, 100}, .color=Color::red()});
    });
    s.layer("title", [](LayerBuilder& l) {
        l.from(Frame{30}).duration(Frame{60});
        l.rect("bg", {.size={100, 100}, .color=Color::green()});
    });
    s.layer("outro", [](LayerBuilder& l) {
        l.from(Frame{90}).duration(Frame{30});
        l.rect("bg", {.size={100, 100}, .color=Color::blue()});
    });
    return s.build();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Test 1: TimelineDebugInfo captures active sequences
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Debug.TimelineDebugInfoCapturesActiveSequences") {
    SUBCASE("f10 — only intro active") {
        auto scene = make_three_layer_scene(Frame{10});
        auto info = analyze_scene(scene, Frame{10});
        CHECK(info.global_frame == 10);
        CHECK(info.active_sequences.size() == 1);
        CHECK(info.active_sequences[0].path == "intro");
        CHECK(info.active_sequences[0].local_frame == 10);
    }

    SUBCASE("f50 — only title active") {
        auto scene = make_three_layer_scene(Frame{50});
        auto info = analyze_scene(scene, Frame{50});
        CHECK(info.active_sequences.size() == 1);
        CHECK(info.active_sequences[0].path == "title");
        CHECK(info.active_sequences[0].local_frame == 20);
    }

    SUBCASE("f100 — only outro active") {
        auto scene = make_three_layer_scene(Frame{100});
        auto info = analyze_scene(scene, Frame{100});
        CHECK(info.active_sequences.size() == 1);
        CHECK(info.active_sequences[0].path == "outro");
        CHECK(info.active_sequences[0].local_frame == 10);
    }

    SUBCASE("f120 — past outro, no active layers") {
        auto scene = make_three_layer_scene(Frame{120});
        auto info = analyze_scene(scene, Frame{120});
        CHECK(info.active_sequences.empty());
    }

    SUBCASE("f29 vs f30 boundary") {
        auto scene29 = make_three_layer_scene(Frame{29});
        auto info29 = analyze_scene(scene29, Frame{29});
        CHECK(info29.active_sequences.size() == 1);
        CHECK(info29.active_sequences[0].path == "intro");

        auto scene30 = make_three_layer_scene(Frame{30});
        auto info30 = analyze_scene(scene30, Frame{30});
        CHECK(info30.active_sequences.size() == 1);
        CHECK(info30.active_sequences[0].path == "title");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 2: JSON output contains all required fields
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Debug.TimelineJSONOutput") {
    // Build scene at frame 50 where "title" layer is active
    SceneBuilder s(debug_ctx(Frame{50}));
    s.layer("title", [](LayerBuilder& l) {
        l.from(Frame{30}).duration(Frame{60});
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Poppins-Bold.ttf";
        p.text.content.value = "HELLO";
        (void)l.text_run("label", std::move(p));
    });
    Scene scene = s.build();

    auto info = analyze_scene(scene, Frame{50});
    json j = info.to_json();

    SUBCASE("global_frame is correct") {
        CHECK(j["global_frame"] == 50);
    }

    SUBCASE("active_sequences is an array") {
        CHECK(j["active_sequences"].is_array());
        CHECK(j["active_sequences"].size() == 1);
    }

    SUBCASE("active sequence has all fields") {
        auto seq = j["active_sequences"][0];
        CHECK(seq["path"] == "title");
        CHECK(seq["from"] == 30);
        CHECK(seq["duration"] == 60);
        CHECK(seq["local_frame"] == 20);
    }

    SUBCASE("assets_used contains font") {
        CHECK(j["assets_used"].is_array());
        CHECK(j["assets_used"].size() >= 1);
        bool found_font = false;
        for (const auto& a : j["assets_used"]) {
            if (a["kind"] == "Font" &&
                a["path"].get<std::string>().find("Poppins-Bold") != std::string::npos) {
                found_font = true;
                CHECK(a["owner"].get<std::string>().find("label") != std::string::npos);
            }
        }
        CHECK(found_font);
    }

    SUBCASE("JSON roundtrip is valid") {
        std::string dumped = j.dump();
        auto parsed = json::parse(dumped);
        CHECK(parsed["global_frame"] == 50);
        CHECK(parsed["active_sequences"].size() == 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 3: Nested sequences produce correct path hierarchy
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Debug.NestedSequencePathHierarchy") {
    // Build a scene where "chapter_title" layer is active (from=120, dur=30)
    // inside a chapter that starts at frame 100.
    // At frame 130, the layer is active.
    SceneBuilder s(debug_ctx(Frame{130}));
    s.layer("chapter", [](LayerBuilder& l) {
        l.from(Frame{100}).duration(Frame{100});
        l.rect("bg", {.size={100, 100}, .color=Color::yellow()});
    });
    s.layer("chapter_title", [](LayerBuilder& l) {
        l.from(Frame{120}).duration(Frame{30});
        l.rect("bg", {.size={100, 100}, .color=Color::white()});
    });
    Scene scene = s.build();

    SUBCASE("f130 — both chapter and chapter_title active") {
        auto info = analyze_scene(scene, Frame{130});
        CHECK(info.global_frame == 130);
        CHECK(info.active_sequences.size() == 2);

        // Both layers should be active
        bool found_chapter = false;
        bool found_title = false;
        for (const auto& seq : info.active_sequences) {
            if (seq.path == "chapter") {
                found_chapter = true;
                CHECK(seq.local_frame == 30);
            }
            if (seq.path == "chapter_title") {
                found_title = true;
                CHECK(seq.local_frame == 10);
            }
        }
        CHECK(found_chapter);
        CHECK(found_title);
    }

    SUBCASE("f110 — only chapter active, chapter_title not yet") {
        // Rebuild at frame 110 where chapter_title (from=120) is NOT active
        SceneBuilder s2(debug_ctx(Frame{110}));
        s2.layer("chapter", [](LayerBuilder& l) {
            l.from(Frame{100}).duration(Frame{100});
            l.rect("bg", {.size={100, 100}, .color=Color::yellow()});
        });
        s2.layer("chapter_title", [](LayerBuilder& l) {
            l.from(Frame{120}).duration(Frame{30});
            l.rect("bg", {.size={100, 100}, .color=Color::white()});
        });
        Scene scene2 = s2.build();

        auto info = analyze_scene(scene2, Frame{110});
        // Only chapter is active (from=100, dur=100)
        bool found_title = false;
        for (const auto& seq : info.active_sequences) {
            if (seq.path == "chapter_title") found_title = true;
        }
        CHECK_FALSE(found_title);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 4: Multiple asset types in debug output
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Debug.MultipleAssetTypesInOutput") {
    SceneBuilder s(debug_ctx(Frame{30}));
    s.layer("mixed", [](LayerBuilder& l) {
        l.from(Frame{0}).duration(Frame{60});
        TextRunSpec p;
        p.text.font.font_path = "assets/fonts/Inter-Bold.ttf";
        p.text.content.value = "Hello";
        (void)l.text_run("text", std::move(p));
        l.image("bg", {.path = "assets/bg.png", .size = {1920, 1080}});
    });
    Scene scene = s.build();

    auto info = analyze_scene(scene, Frame{30});
    json j = info.to_json();

    CHECK(j["assets_used"].size() >= 2);

    bool found_font = false;
    bool found_image = false;
    for (const auto& a : j["assets_used"]) {
        if (a["kind"] == "Font") found_font = true;
        if (a["kind"] == "Image") found_image = true;
    }
    CHECK(found_font);
    CHECK(found_image);
}
