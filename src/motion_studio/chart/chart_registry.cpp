// ═════════════════════════════════════════════════════════════════════════════
//  ChartRegistry::emit_all — flush queued line charts into a SceneBuilder.
//
//  For each queued line chart:
//    • line stroke (Path with move_to + n × line_to)
//    • optional area-fill hint (a single rounded rect filled with alpha
//      gradient as a visual cue)
//    • dot markers (one Circle layer per data point, opacity_anim staggered)
//
//  Draw animation: the line layer's opacity fades 0 → 1 over
//  `draw_animation * fps` frames starting at `opts.start`.  Dots fade in
//  afterwards at `point_stagger` intervals.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/motion_studio/chart/chart_registry.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::motion_studio {

void ChartRegistry::emit_all(chronon3d::SceneBuilder& s, const std::string& prefix) {
    for (const auto& p : pending_) {
        const LineChartOptions& opts = p.opts;
        if (opts.values.size() < 2) continue;

        // ── Range normalisation ──────────────────────────────────────
        f32 min_v = opts.values[0];
        f32 max_v = opts.values[0];
        for (f32 v : opts.values) { min_v = std::min(min_v, v); max_v = std::max(max_v, v); }
        if (max_v == min_v) max_v = min_v + 1.0f;

        const f32 w = opts.size.x;
        const f32 h = opts.size.y;
        const std::size_t n = opts.values.size();

        // ── Polyline ─────────────────────────────────────────────────
        std::vector<chronon3d::PathCommand> cmds;
        cmds.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const f32 t  = (n == 1) ? 0.0f : static_cast<f32>(i) / static_cast<f32>(n - 1);
            const f32 ty = (opts.values[i] - min_v) / (max_v - min_v);
            const chronon3d::Vec2 pt{t * w, (1.0f - ty) * h};
            if (i == 0) cmds.push_back(chronon3d::PathCommand::move_to(pt));
            else        cmds.push_back(chronon3d::PathCommand::line_to(pt));
        }

        const chronon3d::Frame draw_frames(static_cast<chronon3d::Frame>(
            std::lroundf(opts.draw_animation * p.fps)));
        const chronon3d::Frame line_start(static_cast<chronon3d::Frame>(
            std::lroundf(opts.start)));
        const std::string line_id = prefix + "_" + p.id;

        // ── Polyline layer ───────────────────────────────────────────
        s.layer(line_id, [cmds,
                          stroke_color = opts.stroke_color,
                          stroke_width = opts.stroke_width,
                          draw_frames,
                          line_start,
                          size_w = w, size_h = h](chronon3d::LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            l.screen_dimensions(size_w, size_h);
            chronon3d::PathParams pp;
            pp.commands = cmds;
            pp.color    = stroke_color;
            pp.stroke.enabled = true;
            pp.stroke.width   = stroke_width;
            pp.stroke.color   = stroke_color;
            pp.pos = {0.0f, 0.0f, 0.0f};
            l.path("line", pp);

            if (draw_frames > 0) {
                l.opacity_anim()
                    .key(line_start, 0.0f,
                         chronon3d::EasingCurve{chronon3d::Easing::Linear})
                    .key(line_start + draw_frames, 1.0f,
                         chronon3d::EasingCurve{chronon3d::Easing::Linear});
            }
        });

        // ── Area-fill MARKER (visual hint only) ──────────────────────
        if (opts.area_fill_marker) {
            s.layer(line_id + "_area_hint", [fill_color = opts.fill_color,
                                              size_w = w, size_h = h](chronon3d::LayerBuilder& l) {
                chronon3d::RoundedRectParams rp;
                rp.size  = {size_w, size_h * 0.5f};
                rp.color = fill_color;
                rp.pos   = {0.0f, size_h * 0.25f, 0.0f};
                rp.radius = 6.0f;
                l.position({0.0f, 0.0f, 0.0f});
                l.opacity(0.10f);
                l.rounded_rect("bg", rp);
            });
        }

        // ── Dot markers (staggered fade-in AFTER line draws) ─────────
        if (opts.show_points) {
            const chronon3d::Frame stagger(static_cast<chronon3d::Frame>(
                std::lroundf(opts.point_stagger * p.fps)));
            const chronon3d::Frame dot_dur(8);
            for (std::size_t i = 0; i < n; ++i) {
                const f32 t  = (n == 1) ? 0.0f : static_cast<f32>(i) / static_cast<f32>(n - 1);
                const f32 ty = (opts.values[i] - min_v) / (max_v - min_v);
                const chronon3d::Vec2 pt{t * w, (1.0f - ty) * h};
                const chronon3d::Frame dot_start = line_start + draw_frames + chronon3d::Frame(i * stagger);
                const std::string dot_id = line_id + "_dot_" + std::to_string(i);
                s.layer(dot_id, [pt,
                                  radius = opts.dot_radius,
                                  color  = opts.dot_color,
                                  dot_start,
                                  dot_dur](chronon3d::LayerBuilder& l) {
                    l.position({pt.x, pt.y, 0.0f});
                    chronon3d::CircleParams cp;
                    cp.radius = radius;
                    cp.color  = color;
                    cp.pos    = {0, 0, 0};
                    l.circle("pt", cp);
                    l.opacity_anim()
                        .key(dot_start, 0.0f,
                             chronon3d::EasingCurve{chronon3d::Easing::Linear})
                        .key(dot_start + dot_dur, 1.0f,
                             chronon3d::EasingCurve{chronon3d::Easing::Linear});
                });
            }
        }
    }
}

} // namespace chronon3d::motion_studio
