#include <chronon3d/vector/path_factories.hpp>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>

namespace chronon3d {

namespace {
void apply_style(PathParams& path, const ShapeStyle& style, const Color& color) {
    path.fill = style.fill;
    path.stroke = style.stroke;
    path.color = color;
    path.color.a *= style.opacity;
}
} // namespace

std::vector<PathCommand> make_rounded_rect_commands(Vec2 center, Vec2 size, f32 r) {
    f32 w = size.x * 0.5f;
    f32 h = size.y * 0.5f;
    r = std::min({r, w, h});
    std::vector<PathCommand> cmds;
    if (r <= 0.0f) {
        cmds.push_back(PathCommand::move_to({center.x - w, center.y + h}));
        cmds.push_back(PathCommand::line_to({center.x + w, center.y + h}));
        cmds.push_back(PathCommand::line_to({center.x + w, center.y - h}));
        cmds.push_back(PathCommand::line_to({center.x - w, center.y - h}));
        cmds.push_back(PathCommand::close());
    } else {
        cmds.push_back(PathCommand::move_to({center.x - w + r, center.y + h}));
        cmds.push_back(PathCommand::line_to({center.x + w - r, center.y + h}));
        cmds.push_back(PathCommand::quadratic_to({center.x + w, center.y + h}, {center.x + w, center.y + h - r}));
        cmds.push_back(PathCommand::line_to({center.x + w, center.y - h + r}));
        cmds.push_back(PathCommand::quadratic_to({center.x + w, center.y - h}, {center.x + w - r, center.y - h}));
        cmds.push_back(PathCommand::line_to({center.x - w + r, center.y - h}));
        cmds.push_back(PathCommand::quadratic_to({center.x - w, center.y - h}, {center.x - w, center.y - h + r}));
        cmds.push_back(PathCommand::line_to({center.x - w, center.y + h - r}));
        cmds.push_back(PathCommand::quadratic_to({center.x - w, center.y + h}, {center.x - w + r, center.y + h}));
        cmds.push_back(PathCommand::close());
    }
    return cmds;
}

PathParams make_arrow(const ArrowParams& p) {
    PathParams path;
    path.pos = p.pos;
    path.closed = true;
    apply_style(path, p.style, p.color);

    Vec2 dir = p.to - p.from;
    f32 len = glm::length(dir);
    if (len < 1e-4f) {
        return path;
    }
    dir /= len;
    Vec2 normal{-dir.y, dir.x};

    f32 h_len = p.head_length;
    f32 h_wid = p.head_width;

    Vec2 p1 = p.from - normal * (p.thickness * 0.5f);
    Vec2 p2 = (p.to - dir * h_len) - normal * (p.thickness * 0.5f);
    Vec2 p3 = (p.to - dir * h_len) - normal * (h_wid * 0.5f);
    Vec2 p4 = p.to;
    Vec2 p5 = (p.to - dir * h_len) + normal * (h_wid * 0.5f);
    Vec2 p6 = (p.to - dir * h_len) + normal * (p.thickness * 0.5f);
    Vec2 p7 = p.from + normal * (p.thickness * 0.5f);

    path.commands.push_back(PathCommand::move_to(p1));
    path.commands.push_back(PathCommand::line_to(p2));
    path.commands.push_back(PathCommand::line_to(p3));
    path.commands.push_back(PathCommand::line_to(p4));
    path.commands.push_back(PathCommand::line_to(p5));
    path.commands.push_back(PathCommand::line_to(p6));
    path.commands.push_back(PathCommand::line_to(p7));
    path.commands.push_back(PathCommand::close());

    return path;
}

PathParams make_star(const StarParams& p) {
    PathParams path;
    path.pos = p.pos;
    path.closed = true;
    apply_style(path, p.style, p.color);

    int count = p.points * 2;
    f32 angle_step = 3.14159265f / p.points;

    for (int i = 0; i < count; ++i) {
        f32 angle = i * angle_step - 3.14159265f * 0.5f; // point up
        f32 r = (i % 2 == 0) ? p.outer_radius : p.inner_radius;
        Vec2 pt = p.center + Vec2(std::cos(angle), std::sin(angle)) * r;
        if (i == 0) {
            path.commands.push_back(PathCommand::move_to(pt));
        } else {
            path.commands.push_back(PathCommand::line_to(pt));
        }
    }
    path.commands.push_back(PathCommand::close());
    return path;
}

PathParams make_badge(const BadgeParams& p) {
    PathParams path;
    path.pos = p.pos;
    path.closed = true;
    apply_style(path, p.style, p.color);

    int count = p.scallops * 2;
    f32 angle_step = 3.14159265f / p.scallops;

    for (int i = 0; i < count; ++i) {
        f32 angle = i * angle_step;
        f32 r = (i % 2 == 0) ? p.radius : (p.radius - p.scallop_depth);
        Vec2 pt = p.center + Vec2(std::cos(angle), std::sin(angle)) * r;
        if (i == 0) {
            path.commands.push_back(PathCommand::move_to(pt));
        } else {
            path.commands.push_back(PathCommand::line_to(pt));
        }
    }
    path.commands.push_back(PathCommand::close());
    return path;
}

PathParams make_speech_bubble(const SpeechBubbleParams& p) {
    PathParams path;
    path.pos = p.pos;
    path.closed = true;
    apply_style(path, p.style, p.color);

    f32 w = p.size.x * 0.5f;
    f32 h = p.size.y * 0.5f;
    f32 r = std::min({p.corner_radius, w, h});

    // Bubble bounding box local coords: [-w, w] x [-h, h]
    // Top-left to top-right
    path.commands.push_back(PathCommand::move_to({p.center.x - w + r, p.center.y + h}));
    path.commands.push_back(PathCommand::line_to({p.center.x + w - r, p.center.y + h}));
    path.commands.push_back(PathCommand::quadratic_to({p.center.x + w, p.center.y + h}, {p.center.x + w, p.center.y + h - r}));
    
    // Right side
    path.commands.push_back(PathCommand::line_to({p.center.x + w, p.center.y - h + r}));
    path.commands.push_back(PathCommand::quadratic_to({p.center.x + w, p.center.y - h}, {p.center.x + w - r, p.center.y - h}));
    
    // Bottom side with triangle pointer
    f32 half_pw = p.pointer_width * 0.5f;
    f32 pc = p.pointer_center;
    // Ensure base fits inside corners
    f32 left_limit = -w + r;
    f32 right_limit = w - r;
    f32 p_left = std::clamp(pc - half_pw, left_limit, right_limit);
    f32 p_right = std::clamp(pc + half_pw, left_limit, right_limit);

    path.commands.push_back(PathCommand::line_to({p.center.x + p_right, p.center.y - h}));
    path.commands.push_back(PathCommand::line_to({p.center.x + p.pointer_tip.x, p.center.y + p.pointer_tip.y}));
    path.commands.push_back(PathCommand::line_to({p.center.x + p_left, p.center.y - h}));
    
    path.commands.push_back(PathCommand::line_to({p.center.x - w + r, p.center.y - h}));
    path.commands.push_back(PathCommand::quadratic_to({p.center.x - w, p.center.y - h}, {p.center.x - w, p.center.y - h + r}));

    // Left side
    path.commands.push_back(PathCommand::line_to({p.center.x - w, p.center.y + h - r}));
    path.commands.push_back(PathCommand::quadratic_to({p.center.x - w, p.center.y + h}, {p.center.x - w + r, p.center.y + h}));

    path.commands.push_back(PathCommand::close());
    return path;
}

PathParams make_callout(const CalloutParams& p) {
    PathParams path;
    path.pos = p.pos;
    path.closed = true;
    apply_style(path, p.style, p.color);

    f32 w = p.rect_size.x * 0.5f;
    f32 h = p.rect_size.y * 0.5f;
    f32 r = std::min({p.corner_radius, w, h});

    // Detect closest edge to target_point
    Vec2 local_target = p.target_point - p.rect_center;
    enum class Edge { Left, Right, Top, Bottom } edge = Edge::Bottom;

    f32 dist_bottom = std::abs(local_target.y - (-h));
    f32 dist_top = std::abs(local_target.y - h);
    f32 dist_left = std::abs(local_target.x - (-w));
    f32 dist_right = std::abs(local_target.x - w);
    f32 min_dist = dist_bottom;

    if (dist_top < min_dist) { min_dist = dist_top; edge = Edge::Top; }
    if (dist_left < min_dist) { min_dist = dist_left; edge = Edge::Left; }
    if (dist_right < min_dist) { min_dist = dist_right; edge = Edge::Right; }

    f32 half_pw = p.pointer_width * 0.5f;

    // Start drawing
    // Top-left
    path.commands.push_back(PathCommand::move_to({p.rect_center.x - w + r, p.rect_center.y + h}));

    // Top side
    if (edge == Edge::Top) {
        f32 p_left = std::clamp(local_target.x - half_pw, -w + r, w - r);
        f32 p_right = std::clamp(local_target.x + half_pw, -w + r, w - r);
        path.commands.push_back(PathCommand::line_to({p.rect_center.x + p_left, p.rect_center.y + h}));
        path.commands.push_back(PathCommand::line_to({p.target_point.x, p.target_point.y}));
        path.commands.push_back(PathCommand::line_to({p.rect_center.x + p_right, p.rect_center.y + h}));
    }
    path.commands.push_back(PathCommand::line_to({p.rect_center.x + w - r, p.rect_center.y + h}));
    path.commands.push_back(PathCommand::quadratic_to({p.rect_center.x + w, p.rect_center.y + h}, {p.rect_center.x + w, p.rect_center.y + h - r}));

    // Right side
    if (edge == Edge::Right) {
        f32 p_bottom = std::clamp(local_target.y - half_pw, -h + r, h - r);
        f32 p_top = std::clamp(local_target.y + half_pw, -h + r, h - r);
        path.commands.push_back(PathCommand::line_to({p.rect_center.x + w, p.rect_center.y + p_top}));
        path.commands.push_back(PathCommand::line_to({p.target_point.x, p.target_point.y}));
        path.commands.push_back(PathCommand::line_to({p.rect_center.x + w, p.rect_center.y + p_bottom}));
    }
    path.commands.push_back(PathCommand::line_to({p.rect_center.x + w, p.rect_center.y - h + r}));
    path.commands.push_back(PathCommand::quadratic_to({p.rect_center.x + w, p.rect_center.y - h}, {p.rect_center.x + w - r, p.rect_center.y - h}));

    // Bottom side
    if (edge == Edge::Bottom) {
        f32 p_left = std::clamp(local_target.x - half_pw, -w + r, w - r);
        f32 p_right = std::clamp(local_target.x + half_pw, -w + r, w - r);
        path.commands.push_back(PathCommand::line_to({p.rect_center.x + p_right, p.rect_center.y - h}));
        path.commands.push_back(PathCommand::line_to({p.target_point.x, p.target_point.y}));
        path.commands.push_back(PathCommand::line_to({p.rect_center.x + p_left, p.rect_center.y - h}));
    }
    path.commands.push_back(PathCommand::line_to({p.rect_center.x - w + r, p.rect_center.y - h}));
    path.commands.push_back(PathCommand::quadratic_to({p.rect_center.x - w, p.rect_center.y - h}, {p.rect_center.x - w, p.rect_center.y - h + r}));

    // Left side
    if (edge == Edge::Left) {
        f32 p_bottom = std::clamp(local_target.y - half_pw, -h + r, h - r);
        f32 p_top = std::clamp(local_target.y + half_pw, -h + r, h - r);
        path.commands.push_back(PathCommand::line_to({p.rect_center.x - w, p.rect_center.y + p_bottom}));
        path.commands.push_back(PathCommand::line_to({p.target_point.x, p.target_point.y}));
        path.commands.push_back(PathCommand::line_to({p.rect_center.x - w, p.rect_center.y + p_top}));
    }
    path.commands.push_back(PathCommand::line_to({p.rect_center.x - w, p.rect_center.y + h - r}));
    path.commands.push_back(PathCommand::quadratic_to({p.rect_center.x - w, p.rect_center.y + h}, {p.rect_center.x - w + r, p.rect_center.y + h}));

    path.commands.push_back(PathCommand::close());
    return path;
}

} // namespace chronon3d
