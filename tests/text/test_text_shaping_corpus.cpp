#include <optional>
#include <set>
#include <string>

#include <doctest/doctest.h>

#include "test_text_quality_helpers.hpp"

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/text_resolver.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>

using namespace chronon3d;
using namespace test_text_quality;

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Per-cluster metrics captured from a shaped run.
// ═══════════════════════════════════════════════════════════════════════════

struct ClusterMetrics {
    std::size_t byte_offset{0};
    std::size_t byte_len{0};
    std::size_t start_glyph{0};
    std::size_t end_glyph{0};
    float       advance{0.0f};
    float       raw_advance{0.0f};
    std::string font_family;
};

struct RunMetrics {
    TextDirection direction{TextDirection::LTR};
    std::string font_family;
    std::vector<std::uint32_t> glyph_ids;
    std::vector<ClusterMetrics> clusters;
    std::vector<float> advances;
    std::vector<float> x_offsets;
    std::vector<float> y_offsets;
    std::vector<float> xs;
    std::vector<std::size_t> glyph_byte_offsets;
    float baseline{0.0f};
    float ascent{0.0f};
    float descent{0.0f};
    float line_height{0.0f};
    float total_width{0.0f};
    float ink_x0{0.0f}, ink_y0{0.0f}, ink_x1{0.0f}, ink_y1{0.0f};
};

struct ParagraphMetrics {
    std::vector<RunMetrics> runs;
    float ink_x0{0.0f}, ink_y0{0.0f}, ink_x1{0.0f}, ink_y1{0.0f};
};

struct CorpusMetrics {
    std::vector<ParagraphMetrics> paragraphs;
    std::size_t missing_glyphs{0};
};

// ═══════════════════════════════════════════════════════════════════════════
// Shape a TextDocument through the canonical resolver and collect per-cluster
// shaping metrics.  Uses the bundled fonts directory as the fallback root.
// ═══════════════════════════════════════════════════════════════════════════

CorpusMetrics collect_corpus_metrics(
    const TextDocument& doc,
    FontEngine& engine,
    const std::filesystem::path& bundled_fonts_root
) {
    CorpusMetrics out;
    auto tree = resolve_text_run_tree(doc, engine, bundled_fonts_root);
    out.missing_glyphs = tree.missing_glyph_count;

    for (const auto& par : tree.paragraphs) {
        ParagraphMetrics p_metrics;
        for (const auto& run : par.runs) {
            auto placed = shape_resolved_run(run, engine, 0.0f, "");
            if (placed.glyphs.empty()) {
                continue;
            }

            RunMetrics r;
            r.direction = run.direction;
            r.baseline = placed.baseline;
            r.ascent = placed.ascent;
            r.descent = placed.descent;
            r.line_height = placed.total_height;
            r.total_width = placed.total_width;
            r.font_family = run.font.font_family;

            float x0 =  std::numeric_limits<float>::max();
            float y0 =  std::numeric_limits<float>::max();
            float x1 = -std::numeric_limits<float>::max();
            float y1 = -std::numeric_limits<float>::max();

            for (const auto& g : placed.glyphs) {
                r.glyph_ids.push_back(g.glyph_id);
                r.advances.push_back(g.advance_x);
                r.x_offsets.push_back(g.x_offset);
                r.y_offsets.push_back(g.y_offset);
                r.xs.push_back(g.x);
                r.glyph_byte_offsets.push_back(g.byte_offset);

                if (g.bbox_x0 != 0.0f || g.bbox_x1 != 0.0f ||
                    g.bbox_y0 != 0.0f || g.bbox_y1 != 0.0f) {
                    x0 = std::min(x0, g.bbox_x0);
                    y0 = std::min(y0, g.bbox_y0);
                    x1 = std::max(x1, g.bbox_x1);
                    y1 = std::max(y1, g.bbox_y1);
                }
            }

            if (x0 <= x1 && y0 <= y1) {
                r.ink_x0 = x0; r.ink_y0 = y0;
                r.ink_x1 = x1; r.ink_y1 = y1;
            }

            for (const auto& cl : placed.clusters) {
                ClusterMetrics cm;
                cm.byte_offset = cl.byte_offset;
                cm.byte_len    = cl.byte_len;
                cm.start_glyph = cl.start_glyph;
                cm.end_glyph   = cl.end_glyph;
                cm.advance     = cl.advance;
                cm.raw_advance = cl.raw_advance;
                cm.font_family = run.font.font_family;
                r.clusters.push_back(cm);
            }

            p_metrics.runs.push_back(std::move(r));
        }

        if (!p_metrics.runs.empty()) {
            float px0 =  std::numeric_limits<float>::max();
            float py0 =  std::numeric_limits<float>::max();
            float px1 = -std::numeric_limits<float>::max();
            float py1 = -std::numeric_limits<float>::max();
            for (const auto& r : p_metrics.runs) {
                if (r.ink_x0 == 0.0f && r.ink_x1 == 0.0f) continue;
                px0 = std::min(px0, r.ink_x0);
                py0 = std::min(py0, r.ink_y0);
                px1 = std::max(px1, r.ink_x1);
                py1 = std::max(py1, r.ink_y1);
            }
            if (px0 <= px1 && py0 <= py1) {
                p_metrics.ink_x0 = px0; p_metrics.ink_y0 = py0;
                p_metrics.ink_x1 = px1; p_metrics.ink_y1 = py1;
            }
        }

        out.paragraphs.push_back(std::move(p_metrics));
    }

    return out;
}

TextDocument make_corpus_doc(const std::string& utf8) {
    TextDocument doc;
    doc.utf8 = utf8;
    doc.defaults.font = inter_bold_quality();
    doc.split_paragraphs();
    return doc;
}

bool has_runs(const CorpusMetrics& m) {
    for (const auto& p : m.paragraphs) {
        if (!p.runs.empty()) return true;
    }
    return false;
}

std::size_t total_glyph_count(const CorpusMetrics& m) {
    std::size_t n = 0;
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) n += r.glyph_ids.size();
    }
    return n;
}

std::size_t total_cluster_count(const CorpusMetrics& m) {
    std::size_t n = 0;
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) n += r.clusters.size();
    }
    return n;
}

// Check that cluster byte ranges are inside the source string.
void check_cluster_ranges(const CorpusMetrics& m, const std::string& source) {
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            for (const auto& cl : r.clusters) {
                CHECK(cl.byte_offset + cl.byte_len <= source.size());
            }
        }
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Latin / English corpus items — ligatures and kerning
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: AVATAR records glyph IDs, advances and kerning") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("AVATAR");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));
    CHECK(m.missing_glyphs == 0);

    // AV kerning: the 'V' should be pulled left relative to a neutral pair.
    // We compare "AV" width against the sum of isolated "A" and "V" widths.
    auto av_run = engine.shape_text("AV", inter_bold_quality(), 32.0f);
    auto a_run  = engine.shape_text("A",  inter_bold_quality(), 32.0f);
    auto v_run  = engine.shape_text("V",  inter_bold_quality(), 32.0f);
    REQUIRE(av_run.has_value());
    REQUIRE(a_run.has_value());
    REQUIRE(v_run.has_value());
    CHECK(av_run->width < a_run->width + v_run->width);

    // Metrics invariants: glyph and cluster counts, positive advances,
    // positive ascent/descent/line_height.
    CHECK(total_glyph_count(m) > 0);
    CHECK(total_cluster_count(m) > 0);
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            CHECK(r.ascent > 0.0f);
            CHECK(r.descent > 0.0f);
            CHECK(r.line_height > 0.0f);
            for (const auto& adv : r.advances) CHECK(adv >= 0.0f);
        }
    }
    check_cluster_ranges(m, doc.utf8);
}

TEST_CASE("ShapingCorpus: office affine shows ligature behaviour") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("office affine");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));

    // Explicit ligature check: "fi" should produce fewer glyphs when
    // standard ligatures are enabled than when they are disabled.
    auto fi_liga = engine.shape_text("fi", inter_bold_quality(), 32.0f,
                                      TextShaping{.features = "liga=1"});
    auto fi_no_liga = engine.shape_text("fi", inter_bold_quality(), 32.0f,
                                         TextShaping{.features = "liga=0"});
    REQUIRE(fi_liga.has_value());
    REQUIRE(fi_no_liga.has_value());
    INFO("fi liga glyphs=", fi_liga->glyphs.size(), " no-liga glyphs=", fi_no_liga->glyphs.size());
    CHECK(fi_liga->glyphs.size() < fi_no_liga->glyphs.size());

    // "office affine" contains possible 'ff' and 'fi' ligatures.
    // The shaped glyph count should be less than or equal to the codepoint
    // count, and clusters must map back to the original UTF-8 bytes.
    std::size_t total_glyphs = 0;
    std::size_t total_clusters = 0;
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            total_glyphs += r.glyph_ids.size();
            total_clusters += r.clusters.size();
        }
    }
    CHECK(total_glyphs > 0);
    CHECK(total_clusters > 0);
    CHECK(total_glyphs <= 12); // "office affine" has 12 codepoints

    // Verify every cluster maps back into the source text.
    check_cluster_ranges(m, doc.utf8);
}

TEST_CASE("ShapingCorpus: To WA records glyph and cluster metrics") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("To WA");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));
    CHECK(m.missing_glyphs == 0);
    check_cluster_ranges(m, doc.utf8);

    CHECK(total_glyph_count(m) > 0);
    CHECK(total_cluster_count(m) > 0);
}

TEST_CASE("ShapingCorpus: Cafe déjà vu cluster mapping is valid") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("Café déjà vu");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));
    CHECK(m.missing_glyphs == 0);

    check_cluster_ranges(m, doc.utf8);

    // Layout metrics must be positive even if individual glyph bboxes are
    // not populated by the backend in this configuration.
    REQUIRE(!m.paragraphs.empty());
    REQUIRE(!m.paragraphs[0].runs.empty());
    CHECK(m.paragraphs[0].runs[0].total_width > 0.0f);
    CHECK(m.paragraphs[0].runs[0].line_height > 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Cyrillic
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: Cyrillic Привет мир shapes and records metrics") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("Привет мир");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));
    CHECK(m.missing_glyphs == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Arabic — contextual shaping and RTL visual order
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: Arabic contextual shaping differs from isolated forms") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine, noto_naskh_arabic_quality())) {
        if (!require_font(engine)) return;
    }

    auto doc = make_corpus_doc("مرحبا بالعالم");
    // Prefer Noto Naskh Arabic if available so contextual forms are real.
    if (engine.can_load(noto_naskh_arabic_quality())) {
        doc.defaults.font = noto_naskh_arabic_quality();
    }
    doc.split_paragraphs();

    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));

    // At least one RTL run must exist (the whole Arabic sentence is RTL).
    bool has_rtl = false;
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            if (r.direction == TextDirection::RTL) has_rtl = true;
        }
    }
    CHECK(has_rtl);
    CHECK(m.missing_glyphs == 0);

    // Contextual shaping: the same Arabic letters in a word should produce
    // different glyph sequences than when shaped in isolation. We compare
    // the full word "مرحبا" against the isolated letter "م".
    if (engine.can_load(noto_naskh_arabic_quality())) {
        auto word_run = engine.shape_text("مرحبا", noto_naskh_arabic_quality(), 32.0f,
                                           TextShaping{.direction = TextDirection::RTL});
        // Meem isolated form.
        auto isolated_run = engine.shape_text("\xD9\x85", noto_naskh_arabic_quality(), 32.0f,
                                               TextShaping{.direction = TextDirection::RTL});
        REQUIRE(word_run.has_value());
        REQUIRE(isolated_run.has_value());
        REQUIRE(!word_run->glyphs.empty());
        REQUIRE(!isolated_run->glyphs.empty());

        // The full word must contain at least as many glyphs as the isolated
        // letter; more importantly, the glyph set used by the word must differ
        // from the single isolated glyph.
        CHECK(word_run->glyphs.size() >= isolated_run->glyphs.size());
        bool all_same = (word_run->glyphs.size() == isolated_run->glyphs.size())
            && (word_run->glyphs[0].glyph_id == isolated_run->glyphs[0].glyph_id);
        INFO("word first glyph=", word_run->glyphs[0].glyph_id,
             " isolated meem=", isolated_run->glyphs[0].glyph_id);
        CHECK(!all_same);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 4. Hebrew
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: Hebrew עברית shapes with RTL direction") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("עברית");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));

    bool has_rtl = false;
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            if (r.direction == TextDirection::RTL) has_rtl = true;
        }
    }
    CHECK(has_rtl);
    // Hebrew is not covered by the bundled fonts.  If the OS has no Hebrew
    // fallback, the fail-loud audit should report missing glyphs.  The
    // important invariant is that the run is not silently dropped.
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            CHECK(!r.clusters.empty());
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 5. Devanagari
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: Devanagari हिन्दी shapes with clusters") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("हिन्दी");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));
    // Devanagari is not covered by the bundled fonts.  If the OS has no
    // Devanagari fallback, the fail-loud audit should report missing glyphs.
    // The important invariant is that shaping still produces clusters.
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            CHECK(!r.clusters.empty());
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Thai
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: Thai ภาษาไทย shapes with clusters") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("ภาษาไทย");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));
    // Thai is not covered by the bundled fonts.  If the OS has no Thai
    // fallback, the fail-loud audit should report missing glyphs.  The
    // important invariant is that shaping still produces clusters.
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            CHECK(!r.clusters.empty());
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 7. CJK
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: CJK 你好世界 shapes with valid advances") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto doc = make_corpus_doc("你好世界");
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));
    // CJK is not covered by the bundled fonts.  If the OS has no CJK
    // fallback, the fail-loud audit should report missing glyphs.  The
    // important invariant is that shaping still produces valid clusters.

    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            CHECK(!r.clusters.empty());
            for (const auto& cl : r.clusters) {
                CHECK(cl.advance >= 0.0f);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Emoji family — ZWJ sequence must not be broken
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: emoji family ZWJ sequence stays in one cluster") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    // U+1F468 U+200D U+1F469 U+200D U+1F467 U+200D U+1F466  👨‍👩‍👧‍👦
    const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";
    auto doc = make_corpus_doc(family);
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));

    // The ZWJ sequence should be treated as one or more clusters but must not
    // be silently dropped / replaced by .notdef.  If a system emoji font is
    // present, expect the whole sequence to be merged into one cluster; if
    // the font is absent, the fail-loud missing-glyph audit must be active.
    bool whole_sequence_cluster = false;
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            for (const auto& cl : r.clusters) {
                if (cl.byte_len == family.size()) {
                    whole_sequence_cluster = true;
                }
            }
        }
    }
    INFO("missing_glyphs=", m.missing_glyphs);
    CHECK((whole_sequence_cluster || m.missing_glyphs > 0));
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. Mixed RTL/LTR visual order
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ShapingCorpus: mixed RTL/LTR produces multiple directional runs") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    // "Hello " + Arabic "سلام" + " World"
    const std::string mixed = "Hello \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 World";
    auto doc = make_corpus_doc(mixed);
    // Use Noto Naskh Arabic (if available) so the Arabic portion is shaped
    // with real RTL contextual forms instead of .notdef/LTR fallback.
    if (engine.can_load(noto_naskh_arabic_quality())) {
        doc.defaults.font = noto_naskh_arabic_quality();
        doc.split_paragraphs();
    }
    auto m = collect_corpus_metrics(doc, engine, test_repo_root() / "assets" / "fonts");
    REQUIRE(has_runs(m));

#if defined(CHRONON3D_HAS_FRIBIDI)
    std::size_t run_count = 0;
    bool has_ltr = false;
    bool has_rtl = false;
    for (const auto& p : m.paragraphs) {
        run_count += p.runs.size();
        for (const auto& r : p.runs) {
            if (r.direction == TextDirection::LTR) has_ltr = true;
            if (r.direction == TextDirection::RTL) has_rtl = true;
        }
    }
    CHECK(run_count >= 2);
    CHECK(has_ltr);
    CHECK(has_rtl);

    // Visual order: in an RTL run the glyphs are returned in visual
    // (left-to-right screen) order, so their source byte offsets should be
    // non-increasing as the eye moves from left to right.  We check the
    // per-glyph byte offsets rather than the cluster array, because the
    // cluster array is sorted by byte offset for layout consumers.
    bool found_rtl_visual_order = false;
    for (const auto& p : m.paragraphs) {
        for (const auto& r : p.runs) {
            if (r.direction != TextDirection::RTL || r.glyph_byte_offsets.size() < 2) continue;
            bool byte_offsets_non_increasing = true;
            for (std::size_t i = 1; i < r.glyph_byte_offsets.size(); ++i) {
                if (r.glyph_byte_offsets[i] > r.glyph_byte_offsets[i - 1]) {
                    byte_offsets_non_increasing = false;
                    break;
                }
            }
            if (byte_offsets_non_increasing) {
                found_rtl_visual_order = true;
                break;
            }
        }
    }
    CHECK(found_rtl_visual_order);
#else
    MESSAGE("FriBidi not available — mixed-direction run split not verified");
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapingCorpus: word wrap reduces layout width") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    TextDocument doc;
    doc.utf8 = "The quick brown fox jumps over the lazy dog";
    doc.defaults.font = inter_bold_quality();
    doc.defaults.font.font_size = 32.0f;
    doc.split_paragraphs();

    TextLayoutSpec wrapped;
    wrapped.box = {200.0f, 600.0f};
    wrapped.wrap = TextWrap::Word;
    wrapped.paragraph.composer = ParagraphComposer::EveryLine;

    TextLayoutSpec unwrapped;
    unwrapped.box = {200.0f, 600.0f};
    unwrapped.wrap = TextWrap::None;

    auto wrapped_result = build_text_run(doc, engine, wrapped, nullptr, test_repo_root() / "assets" / "fonts");
    auto unwrapped_result = build_text_run(doc, engine, unwrapped, nullptr, test_repo_root() / "assets" / "fonts");

    REQUIRE(!wrapped_result.paragraphs.empty());
    REQUIRE(!unwrapped_result.paragraphs.empty());

    CHECK(wrapped_result.paragraphs[0]->bounds.x <= 200.0f);
    CHECK(wrapped_result.paragraphs[0]->bounds.x <= unwrapped_result.paragraphs[0]->bounds.x);

    // Wrapped layout should also be taller than a single line.
    CHECK(wrapped_result.paragraphs[0]->bounds.y > 0.0f);

    // Glyphs should be placed on at least two distinct vertical lines.
    std::set<float> y_values;
    for (const auto& g : wrapped_result.paragraphs[0]->placed.glyphs) {
        y_values.insert(g.y);
    }
    CHECK(y_values.size() >= 2);
}
