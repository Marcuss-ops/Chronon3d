#include "content/common/text/glyph_layout.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chronon3d::content::text_reveal {

namespace {

// ── Shape-call counter (TICKET-FIX-TEXT-SHAPING-DEDUP-V1) ────────────
//
// File-static atomic so the counter survives across ShapedGlyphLine
// instances.  Tests reset before each measurement (see reset_shape_call_counter).
//
// Memory ordering: relaxed is sufficient — the only consumer is the
// per-test assertion; no producer/consumer ordering required.
// std::atomic<int>> per C++20 std (earlier std::atomic<int>).
std::atomic<int> s_shape_calls_per_line{0};

} // anonymous namespace

// ── Test/internal support (exposed via glyph_layout_test_support.hpp) ───
//
// Surface removed from the public header; kept here so tests can verify
// the single-shape-call contract and inspect the cached GlyphRun.
namespace test_support {

void reset_shape_call_counter() noexcept {
    s_shape_calls_per_line.store(0, std::memory_order_relaxed);
}

int get_shape_call_count() noexcept {
    return s_shape_calls_per_line.load(std::memory_order_relaxed);
}

const std::optional<GlyphRun>& get_raw_run(const ShapedGlyphLine& line) noexcept {
    return line.m_run;
}

} // namespace test_support

// ── Removed direct construction bridge ─────────────────────────────────
//
// ShapedGlyphLine has no public constructor that accepts raw text. The
// previous compatibility constructor was removed under the
// TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL chore; callers now enter through
// the canonical `shape_glyph_line(...)` primitive.

// ── ShapedGlyphLine private ctor (used by canonical shaping) ────────────
//
// Private ctor populates fields from a valid GlyphRun directly — it does
// not shape or throw. It is called by `shape_glyph_line(...)` after the
// engine has produced a valid run.
ShapedGlyphLine::ShapedGlyphLine(GlyphRun run, std::string text,
                                 f32 tracking, f32 ref_offset_x)
    : m_text(std::move(text)), m_tracking(tracking), m_ref_offset_x(ref_offset_x),
      m_run(std::move(run))
{}

// ── ShapedGlyphLine prefix-advances cache ───────────────────────────────
//
// Rebuilds the m_prefix_advances vector so that cursor_position(i) and
// cursor_at_end() are O(1).  m_prefix_advances[0] == m_ref_offset_x and
// m_prefix_advances[i+1] == m_prefix_advances[i] + advance_x + tracking.
// Called once from the factory-created instance; kept const-noexcept because the
// cursor accessors are const-noexcept and the vector is mutable.
void ShapedGlyphLine::rebuild_prefix_advances() {
    if (!m_run) {
        m_prefix_advances.clear();
        m_prefix_advances.push_back(m_ref_offset_x);
        return;
    }
    const size_t n = m_run->glyphs.size();
    m_prefix_advances.resize(n + 1);
    m_prefix_advances[0] = m_ref_offset_x;
    for (size_t i = 0; i < n; ++i) {
        m_prefix_advances[i + 1] = m_prefix_advances[i]
                                   + m_run->glyphs[i].advance_x
                                   + m_tracking;
    }
}

// ── ShapedGlyphLine read-only accessors (unchanged from upstream) ────────
//
// These methods read from `m_run` + `m_tracking` + `m_ref_offset_x`
// (cached state populated by the canonical shape_glyph_line primitive).
// No re-shape
// calls — single engine.shape_text invocation per ShapedGlyphLine
// instance (Point 8 single-shape efficiency).
f32 ShapedGlyphLine::width() const noexcept {
    if (!m_run) return 0.0f;
    const size_t n = m_run->glyphs.size();
    return m_run->width + m_tracking * static_cast<f32>(n > 1 ? n - 1 : 0);
}

// Build per-glyph cluster spans in O(text_size) by finding, for each glyph,
// the leftmost glyph (by index) whose cluster value is strictly greater.
// The legacy O(n²) inner scan iterated j = 0..n-1 and took the cluster value
// of the first glyph with cluster > start.  That leftmost-greater semantics
// is direction-agnostic (it is NOT the same as next-greater-to-the-right),
// so a single monotonic stack is insufficient for RTL runs where clusters
// decrease along visual order.
//
// Algorithm:
//   1. first_index[c] = smallest index j with glyphs[j].cluster == c.
//   2. Scan cluster values from max_cluster down to 0, maintaining the
//      cluster value whose first_index is smallest — that is the cluster
//      value of the leftmost glyph strictly greater than the current value.
//   3. For each glyph, end = leftmost_greater[glyphs[i].cluster].
//
// The scan is O(max_cluster) and max_cluster <= text.size(), so the whole
// build is O(text_size) = O(n) for UTF-8 where text bytes are bounded by
// a constant multiple of glyph count.
/*static*/ std::vector<GlyphClusterSpan> GlyphClusterSpan::build(
    const std::vector<GlyphPosition>& glyphs,
    std::string_view text,
    f32 tracking)
{
    const size_t n = glyphs.size();
    std::vector<GlyphClusterSpan> spans;
    spans.reserve(n);

    if (n == 0) return spans;

    size_t max_cluster = 0;
    for (const auto& g : glyphs) {
        if (g.cluster > max_cluster) max_cluster = g.cluster;
    }

    constexpr size_t kNoIndex = std::numeric_limits<size_t>::max();
    constexpr size_t kNoGreater = std::numeric_limits<size_t>::max();

    // first_index[c] = smallest glyph index with cluster == c, or kNoIndex.
    std::vector<size_t> first_index(max_cluster + 1, kNoIndex);
    for (size_t i = 0; i < n; ++i) {
        const size_t c = glyphs[i].cluster;
        if (c <= max_cluster && first_index[c] == kNoIndex) {
            first_index[c] = i;
        }
    }

    // leftmost_greater[c] = cluster value of the leftmost glyph with
    // cluster > c, or kNoGreater if none exists.
    std::vector<size_t> leftmost_greater(max_cluster + 1, kNoGreater);
    size_t min_first_index = kNoIndex;
    size_t min_first_cluster = kNoGreater;
    for (size_t c = max_cluster + 1; c-- > 0; ) {
        leftmost_greater[c] = min_first_cluster;
        if (first_index[c] < min_first_index) {
            min_first_index = first_index[c];
            min_first_cluster = c;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        const size_t start = glyphs[i].cluster;
        const size_t end = (start <= max_cluster && leftmost_greater[start] != kNoGreater)
                               ? leftmost_greater[start]
                               : text.size();
        spans.push_back({i, i + 1, start, end - start,
                         glyphs[i].advance_x + tracking});
    }
    return spans;
}

std::vector<GlyphPos> ShapedGlyphLine::layout() const {
    std::vector<GlyphPos> out;
    if (!m_run) return out;

    const auto& glyphs = m_run->glyphs;
    const auto spans = GlyphClusterSpan::build(glyphs, m_text, m_tracking);
    out.reserve(spans.size());

    for (size_t gi = 0; gi < spans.size(); ++gi) {
        const auto& span = spans[gi];
        std::string ch = m_text.substr(span.byte_offset, span.byte_len);
        if (ch.empty()) continue;
        const auto& g = glyphs[gi];
        const f32 cursor = m_prefix_advances[gi];
        out.push_back({ch, cursor + g.advance_x * 0.5f, g.advance_x});
    }
    return out;
}

f32 ShapedGlyphLine::cursor_position(size_t index) const noexcept {
    if (m_prefix_advances.empty()) {
        return m_ref_offset_x;
    }
    const size_t n = m_prefix_advances.size() - 1;
    return m_prefix_advances[std::min(index, n)];
}

f32 ShapedGlyphLine::cursor_at_end() const noexcept {
    if (m_prefix_advances.empty()) {
        return m_ref_offset_x;
    }
    return m_prefix_advances.back();
}

GlyphLineBBox ShapedGlyphLine::bbox() const noexcept {
    GlyphLineBBox box;
    box.x0 = m_ref_offset_x;
    box.x1 = m_ref_offset_x;
    if (!m_run || m_run->glyphs.empty()) return box;

    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();
    // Glyph bboxes use a y-up convention where bbox_y0 is the top and
    // bbox_y1 is the bottom, so y0 can be greater than y1. Normalize the
    // final box so callers can rely on x0<=x1 and y0<=y1.

    f32 cursor = m_ref_offset_x;
    for (const auto& g : m_run->glyphs) {
        const f32 gx0 = cursor + g.bbox_x0;
        const f32 gy0 = g.bbox_y0;
        const f32 gx1 = cursor + g.bbox_x1;
        const f32 gy1 = g.bbox_y1;

        min_x = std::min(min_x, gx0);
        min_y = std::min(min_y, gy0);
        max_x = std::max(max_x, gx1);
        max_y = std::max(max_y, gy1);

        cursor += g.advance_x + m_tracking;
    }

    box.x0 = min_x;
    box.y0 = std::min(min_y, max_y);
    box.x1 = max_x;
    box.y1 = std::max(min_y, max_y);
    return box;
}

size_t ShapedGlyphLine::reveal_count(f32 progress) const noexcept {
    if (!m_run) return 0;
    if (progress <= 0.0f) return 0;
    if (progress >= 1.0f) return m_run->glyphs.size();
    return static_cast<size_t>(static_cast<f32>(m_run->glyphs.size()) * progress);
}

// ── shape_glyph_line canonical primitive ────────────────────────────────
//
// One FontEngine::shape_text call produces one immutable ShapedGlyphLine
// snapshot. The reference offset is stored in that snapshot so width,
// cursor and layout accessors share one coordinate contract.
[[nodiscard]] std::optional<ShapedGlyphLine> shape_glyph_line(
    std::string_view text, f32 font_size, const FontSpec& font,
    f32 tracking, f32 ref_offset_x, FontEngine& engine)
{
    auto run_opt = engine.shape_text(std::string(text), font, font_size);
    s_shape_calls_per_line.fetch_add(1, std::memory_order_relaxed);
    if (!run_opt || run_opt->glyphs.empty()) return std::nullopt;

    ShapedGlyphLine line(
        std::move(*run_opt), std::string(text), tracking, ref_offset_x);
    line.rebuild_prefix_advances();
    return line;
}

} // namespace chronon3d::content::text_reveal
