// =============================================================================
// cache_diagnostics_format.cpp — Human-readable cache snapshot dump.
//
// cache_diagnostics_format.cpp — format_cache_snapshot() queries the
// CacheDiagnostics singleton and returns a multi-line string suitable for
// spdlog or CLI output.
// =============================================================================

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <fmt/format.h>
#include <string>

namespace chronon3d::cache {

std::string format_cache_snapshot() {
    auto domains = CacheDiagnostics::instance().snapshot_all_domains();

    if (domains.empty()) {
        return "[cache] No caches registered.\n";
    }

    std::string out;
    out += "┌──── Cache Snapshot ";
    out += std::string(48, '─');
    out += "\n";

    std::size_t total_hits = 0;
    std::size_t total_misses = 0;
    std::size_t total_evictions = 0;
    std::size_t total_entries = 0;
    std::size_t total_weight = 0;
    std::size_t total_capacity = 0;

    // Format weight with appropriate unit (used in loop and totals).
    auto format_weight = [](std::size_t bytes) -> std::string {
        if (bytes >= 1024 * 1024 * 1024) {
            return fmt::format("{:.1f} GB",
                static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        } else if (bytes >= 1024 * 1024) {
            return fmt::format("{:.1f} MB",
                static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024) {
            return fmt::format("{:.1f} KB",
                static_cast<double>(bytes) / 1024.0);
        }
        return fmt::format("{} B", bytes);
    };

    for (const auto& ds : domains) {
        // Human-readable domain name.
        const char* name = "?";
        switch (ds.domain) {
            case CacheDomain::Nodes:            name = "Nodes"; break;
            case CacheDomain::RenderedFrames:   name = "RenderedFrames"; break;
            case CacheDomain::VideoFrames:      name = "VideoFrames"; break;
            case CacheDomain::ConvertedFrames:  name = "ConvertedFrames"; break;
            case CacheDomain::ScenePrograms:    name = "ScenePrograms"; break;
            case CacheDomain::Images:           name = "Images"; break;
            case CacheDomain::GlyphAtlas:       name = "GlyphAtlas"; break;
            case CacheDomain::Text:             name = "Text"; break;
            case CacheDomain::Shadows:          name = "Shadows"; break;
            case CacheDomain::Glow:             name = "Glow"; break;
        }

        const double hit_pct = (ds.hits + ds.misses) > 0
            ? 100.0 * static_cast<double>(ds.hits) / static_cast<double>(ds.hits + ds.misses)
            : 0.0;

        const bool is_bytes =
            (ds.domain == CacheDomain::Nodes ||
             ds.domain == CacheDomain::RenderedFrames ||
             ds.domain == CacheDomain::VideoFrames ||
             ds.domain == CacheDomain::ConvertedFrames ||
             ds.domain == CacheDomain::Images ||
             ds.domain == CacheDomain::Text ||
             ds.domain == CacheDomain::Shadows ||
             ds.domain == CacheDomain::Glow);

        const std::string cap_str = is_bytes
            ? format_weight(ds.total_capacity)
            : fmt::format("{} entries", ds.total_capacity);
        const std::string wt_str = is_bytes
            ? format_weight(ds.current_weight)
            : fmt::format("{} entries", ds.current_weight);

        out += fmt::format(
            "│ {:18s}   hits={:<8} miss={:<8} evict={:<4} size={:<6} weight={:>10s}  cap={:>12s}  hit_rate={:.1f}%\n",
            name, ds.hits, ds.misses, ds.evictions,
            ds.current_size, wt_str, cap_str, hit_pct);

        total_hits      += ds.hits;
        total_misses    += ds.misses;
        total_evictions += ds.evictions;
        total_entries   += ds.current_size;
        total_weight    += ds.current_weight;
        total_capacity  += ds.total_capacity;
    }

    const double total_hit_pct = (total_hits + total_misses) > 0
        ? 100.0 * static_cast<double>(total_hits) / static_cast<double>(total_hits + total_misses)
        : 0.0;

    out += "├──── Totals ";
    out += std::string(48, '─');

    // Format totals with human-readable units (use first domain's unit as
    // heuristic — by default all registered domains use consistent units).
    const bool totals_are_bytes = !domains.empty() &&
        (domains[0].domain != CacheDomain::ScenePrograms &&
         domains[0].domain != CacheDomain::GlyphAtlas);
    const std::string tot_weight_str = totals_are_bytes
        ? format_weight(total_weight)
        : fmt::format("{} entries", total_weight);
    const std::string tot_cap_str = totals_are_bytes
        ? format_weight(total_capacity)
        : fmt::format("{} entries", total_capacity);

    out += fmt::format("\n│ {:18s}   hits={:<8} miss={:<8} evict={:<4} size={:<6} weight={:>10s}  cap={:>12s}  hit_rate={:.1f}%\n",
        fmt::format("{} domains", domains.size()),
        total_hits, total_misses, total_evictions,
        total_entries, tot_weight_str, tot_cap_str, total_hit_pct);

    out += "└";
    out += std::string(79, '─');
    out += "\n";

    return out;
}

} // namespace chronon3d::cache
