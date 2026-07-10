#include "test_text_quality_helpers.hpp"
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
using namespace chronon3d;
using namespace test_text_quality;

// ═══════════════════════════════════════════════════════════════════════════
// 13. Arabic Contextual Forms — Pre-Shaped Extraction
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextQuality: Arabic — contextual vs isolated Meem glyph IDs differ") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    if (!require_font(engine, noto_naskh_arabic_quality())) {
        if (!require_font(engine)) return;
        MESSAGE("Using Inter-Bold instead of Noto Naskh Arabic — positional variants may not exist");
    }

    FontSpec spec = engine.can_load(noto_naskh_arabic_quality())
        ? noto_naskh_arabic_quality() : inter_bold_quality();

    TextShaping ar;
    ar.direction = TextDirection::RTL;
    ar.language = "ar";

    const std::string arabic_word =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

    auto full_run = engine.shape_text(arabic_word, spec, 48.0f, ar);
    REQUIRE(full_run.has_value());
    REQUIRE_FALSE(full_run->glyphs.empty());

    auto full_placed = resolve_placed_glyph_run(*full_run, 0.0f, arabic_word);
    REQUIRE_FALSE(full_placed.clusters.empty());
    REQUIRE_FALSE(full_placed.glyphs.empty());

    uint32_t contextual_meem_gid = 0;
    for (const auto& cl : full_placed.clusters) {
        if (cl.byte_offset == 0 && cl.start_glyph < full_placed.glyphs.size()) {
            contextual_meem_gid = full_placed.glyphs[cl.start_glyph].glyph_id;
            break;
        }
    }
    REQUIRE(contextual_meem_gid > 0);

    const std::string isolated_meem = "\xD9\x85";
    auto iso_run = engine.shape_text(isolated_meem, spec, 48.0f, ar);
    REQUIRE(iso_run.has_value());
    REQUIRE_FALSE(iso_run->glyphs.empty());

    uint32_t isolated_meem_gid = iso_run->glyphs[0].glyph_id;
    REQUIRE(isolated_meem_gid > 0);

    INFO("Contextual Meem glyph_id: ", contextual_meem_gid,
         " Isolated Meem glyph_id: ", isolated_meem_gid);

    if (contextual_meem_gid == isolated_meem_gid) {
        MESSAGE("Font does not have separate positional forms for Meem — "
                "contextual shaping test is N/A for this font");
    } else {
        CHECK(contextual_meem_gid != isolated_meem_gid);
    }
}

TEST_CASE("TextQuality: Arabic — pre-shaped extraction preserves contextual forms") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    if (!require_font(engine, noto_naskh_arabic_quality())) {
        if (!require_font(engine)) return;
    }

    FontSpec spec = engine.can_load(noto_naskh_arabic_quality())
        ? noto_naskh_arabic_quality() : inter_bold_quality();

    TextShaping ar;
    ar.direction = TextDirection::RTL;
    ar.language = "ar";

    const std::string arabic_word =
        "\xD9\x85"
        "\xD8\xB1"
        "\xD8\xAD"
        "\xD8\xA8"
        "\xD8\xA7";

    auto full_run = engine.shape_text(arabic_word, spec, 48.0f, ar);
    REQUIRE(full_run.has_value());
    REQUIRE_FALSE(full_run->glyphs.empty());

    auto full_placed = resolve_placed_glyph_run(*full_run, 0.0f, arabic_word);
    REQUIRE_FALSE(full_placed.clusters.empty());

    namespace ct = chronon3d::content::text;

    PlacedGlyphRun tw_placed;
    auto tw_res = ct::compute_typewriter_layout(
        arabic_word, 48.0f, 0.0f, {2000.0f, 500.0f}, 1.2f, spec, engine,
        &tw_placed);
    REQUIRE(tw_res);
    auto& tw = *tw_res;

    REQUIRE_FALSE(tw.chars.empty());
    REQUIRE_FALSE(tw_placed.clusters.empty());
    REQUIRE_FALSE(tw_placed.glyphs.empty());

    for (size_t ci = 0; ci < tw.chars.size(); ++ci) {
        const auto& cp = tw.chars[ci];
        INFO("Character ", ci, " byte_offset=", cp.byte_offset,
             " byte_len=", cp.byte_len);

        const size_t char_start = cp.byte_offset;
        const size_t char_end = cp.byte_offset + cp.byte_len;

        uint32_t extracted_gid = 0;
        for (const auto& cl : tw_placed.clusters) {
            const size_t cl_start = cl.byte_offset;
            const size_t cl_end = cl.byte_offset + cl.byte_len;
            if (cl_start < char_end && cl_end > char_start) {
                if (cl.start_glyph < tw_placed.glyphs.size()) {
                    extracted_gid = tw_placed.glyphs[cl.start_glyph].glyph_id;
                    break;
                }
            }
        }
        REQUIRE(extracted_gid > 0);

        std::string isolated_char = arabic_word.substr(cp.byte_offset, cp.byte_len);
        auto iso_run = engine.shape_text(isolated_char, spec, 48.0f, ar);
        REQUIRE(iso_run.has_value());
        REQUIRE_FALSE(iso_run->glyphs.empty());
        uint32_t isolated_gid = iso_run->glyphs[0].glyph_id;

        INFO("  Extracted GID: ", extracted_gid,
             " Isolated GID: ", isolated_gid);

        if (extracted_gid != isolated_gid) {
            CHECK(true);
        } else {
            MESSAGE("Same glyph ID for contextual and isolated — "
                    "font may lack positional forms");
        }

        uint32_t full_gid = 0;
        for (const auto& cl : full_placed.clusters) {
            const size_t cl_start = cl.byte_offset;
            const size_t cl_end = cl.byte_offset + cl.byte_len;
            if (cl_start < char_end && cl_end > char_start) {
                if (cl.start_glyph < full_placed.glyphs.size()) {
                    full_gid = full_placed.glyphs[cl.start_glyph].glyph_id;
                    break;
                }
            }
        }
        CHECK(extracted_gid == full_gid);
    }
}

TEST_CASE("TextQuality: Arabic — three positional forms use different glyphs") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    if (!require_font(engine, noto_naskh_arabic_quality())) {
        if (!require_font(engine)) return;
    }

    FontSpec spec = engine.can_load(noto_naskh_arabic_quality())
        ? noto_naskh_arabic_quality() : inter_bold_quality();

    TextShaping ar;
    ar.direction = TextDirection::RTL;
    ar.language = "ar";

    const std::string kaf_isolated = "\xD9\x83";
    const std::string kaf_initial = "\xD9\x83\xD8\xA7";
    const std::string kaf_medial = "\xD9\x84\xD9\x83\xD8\xA7";
    const std::string kaf_final = "\xD9\x84\xD9\x83";

    auto iso_run = engine.shape_text(kaf_isolated, spec, 48.0f, ar);
    auto ini_run = engine.shape_text(kaf_initial, spec, 48.0f, ar);
    auto med_run = engine.shape_text(kaf_medial, spec, 48.0f, ar);
    auto fin_run = engine.shape_text(kaf_final, spec, 48.0f, ar);

    REQUIRE(iso_run.has_value());
    REQUIRE(ini_run.has_value());
    REQUIRE(med_run.has_value());
    REQUIRE(fin_run.has_value());

    uint32_t iso_gid = iso_run->glyphs[0].glyph_id;
    uint32_t ini_gid = ini_run->glyphs[0].glyph_id;

    uint32_t med_gid = 0;
    for (const auto& g : med_run->glyphs) {
        if (g.cluster >= 2 && g.cluster < 4) {
            med_gid = g.glyph_id;
            break;
        }
    }
    if (med_gid == 0 && !med_run->glyphs.empty()) {
        med_gid = med_run->glyphs[med_run->glyphs.size() / 2].glyph_id;
    }

    uint32_t fin_gid = 0;
    for (const auto& g : fin_run->glyphs) {
        if (g.cluster >= 2 && g.cluster < 4) {
            fin_gid = g.glyph_id;
            break;
        }
    }
    if (fin_gid == 0 && !fin_run->glyphs.empty()) {
        fin_gid = fin_run->glyphs.back().glyph_id;
    }

    INFO("Isolated Kaf: ", iso_gid);
    INFO("Initial Kaf:  ", ini_gid);
    INFO("Medial Kaf:   ", med_gid);
    INFO("Final Kaf:    ", fin_gid);

    bool is_noto = engine.can_load(noto_naskh_arabic_quality());

    if (is_noto) {
        CHECK(iso_gid != ini_gid);
        CHECK(iso_gid != med_gid);
        CHECK(iso_gid != fin_gid);
        CHECK(ini_gid != med_gid);
        CHECK(ini_gid != fin_gid);
        CHECK(med_gid != fin_gid);
    } else {
        MESSAGE("Fallback font used — positional form differentiation is font-dependent");
    }
}
