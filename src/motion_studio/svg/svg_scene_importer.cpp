// ═════════════════════════════════════════════════════════════════════════════
//  SvgSceneImporter implementation — minimal recursive SVG document parser.
//
//  Used by `parse_text` on a raw SVG string.  Element-wise tokenisation
//  (no full XML parser) handles the most common attributes:
//
//    <svg viewBox="minX minY w h">
//    <g  transform="translate(x,y) | scale(sx,sy) | rotate(deg) | matrix(a,b,c,d,e,f)">
//    <path d="…"            fill="…" stroke="…" stroke-width="…">
//    <rect  x="…" y="…" width="…" height="…" fill="…" stroke="…" />
//    <circle cx="…" cy="…" r="…" fill="…" stroke="…" />
//    id="…"
//
//  Coordinates are SVG-space (top-left origin, Y-down).  Consumers
//  translate them with the `origin`/`scale` parameters on `apply()`.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/motion_studio/svg/svg_scene_importer.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace chronon3d::motion_studio {

// ─── Content bbox helpers ─────────────────────────────────────────────

Vec2 SvgScene::content_min() const {
    if (elements.empty()) return Vec2{0, 0};
    Vec2 m{elements[0].bbox_min.x, elements[0].bbox_min.y};
    for (const auto& e : elements) {
        m.x = std::min(m.x, e.bbox_min.x); m.y = std::min(m.y, e.bbox_min.y);
    } return m;
}

Vec2 SvgScene::content_max() const {
    if (elements.empty()) return Vec2{0, 0};
    Vec2 m{elements[0].bbox_max.x, elements[0].bbox_max.y};
    for (const auto& e : elements) {
        m.x = std::max(m.x, e.bbox_max.x); m.y = std::max(m.y, e.bbox_max.y);
    } return m;
}

// ─── String helpers ────────────────────────────────────────────────────

namespace {

std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    auto not_ws = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
    return s;
}

// Attribute map flattened out per element.
using Attrs = std::unordered_map<std::string, std::string>;

Attrs parse_attrs(const std::string& raw) {
    Attrs out;
    std::size_t i = 0;
    while (i < raw.size()) {
        // NAME
        std::size_t name_start = i;
        while (i < raw.size() && raw[i] != '=' && !std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
        std::string name = to_lower(raw.substr(name_start, i - name_start));
        // Skip whitespace before =
        while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
        if (i >= raw.size() || raw[i] != '=') {
            if (!name.empty()) out[name] = ""; // bare attribute
            continue;
        }
        ++i; // skip '='
        while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
        if (i >= raw.size()) break;
        char quote = raw[i];
        std::string value;
        if (quote == '"' || quote == '\'') {
            ++i;
            std::size_t vstart = i;
            while (i < raw.size() && raw[i] != quote) ++i;
            value = raw.substr(vstart, i - vstart);
            if (i < raw.size()) ++i;
        } else {
            std::size_t vstart = i;
            while (i < raw.size() && !std::isspace(static_cast<unsigned char>(raw[i])) && raw[i] != '>') ++i;
            value = raw.substr(vstart, i - vstart);
        }
        if (!name.empty()) out[name] = value;
    }
    return out;
}

bool parse_color(const std::string& s, chronon3d::Color& out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    if (t[0] == '#') {
        std::string hex = t.substr(1);
        if (hex.size() == 3) {
            char r = hex[0], g = hex[1], b = hex[2];
            hex = std::string{r, r, g, g, b, b};
        }
        if (hex.size() != 6) return false;
        auto h2i = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
            return 0;
        };
        int r = h2i(hex[0]) * 16 + h2i(hex[1]);
        int g = h2i(hex[2]) * 16 + h2i(hex[3]);
        int b = h2i(hex[4]) * 16 + h2i(hex[5]);
        out = chronon3d::Color{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
        return true;
    }
    return false;
}

float parse_float(const std::string& s, float dflt = 0.0f) {
    try { return std::stof(trim(s)); } catch (...) { return dflt; }
}

// Apply a single SVG transform list entry to the cumulative matrix.
// We represent a 2D transform by a struct {a,b,c,d,e,f} corresponding to
//     [ a c e ]
//     [ b d f ]
//     [ 0 0 1 ]
struct Mat2D { float a=1, b=0, c=0, d=1, e=0, f=0; };

Mat2D apply(const Mat2D& m, const std::string& fn_raw) {
    Mat2D out = m;
    auto inner = fn_raw;
    if (inner.empty()) return out;
    auto lparen = inner.find('(');
    if (lparen == std::string::npos) return out;
    inner = inner.substr(lparen + 1);
    auto rparen = inner.find(')');
    if (rparen != std::string::npos) inner = inner.substr(0, rparen);
    auto fields = [&]() {
        std::vector<float> v;
        std::stringstream ss(inner);
        std::string tok;
        while (ss >> tok) {
            try { v.push_back(std::stof(tok)); } catch (...) {}
        }
        return v;
    }();

    std::string name = to_lower(trim(fn_raw.substr(0, lparen)));
    if (name == "translate") {
        float tx = fields.size() > 0 ? fields[0] : 0;
        float ty = fields.size() > 1 ? fields[1] : 0;
        Mat2D t; t.e = tx; t.f = ty;
        out = Mat2D{
            m.a * t.a, m.b * t.a,
            m.c * t.d, m.d * t.d,
            m.a * t.e + m.c * t.f + m.e,
            m.b * t.e + m.d * t.f + m.f,
        };
    } else if (name == "scale") {
        float sx = fields.size() > 0 ? fields[0] : 1;
        float sy = fields.size() > 1 ? fields[1] : sx;
        out = Mat2D{
            m.a * sx, m.b * sx,
            m.c * sy, m.d * sy,
            m.e, m.f,
        };
    } else if (name == "rotate") {
        float deg = fields.size() > 0 ? fields[0] : 0;
        float c_r = std::cos(deg * 3.14159265f / 180.0f);
        float s_r = std::sin(deg * 3.14159265f / 180.0f);
        out = Mat2D{
            m.a * c_r - m.c * s_r, m.b * c_r - m.d * s_r,
            m.a * s_r + m.c * c_r, m.b * s_r + m.d * c_r,
            m.e, m.f,
        };
    } else if (name == "matrix" && fields.size() >= 6) {
        float na = fields[0], nb = fields[1], nc = fields[2],
              nd = fields[3], ne = fields[4], nf = fields[5];
        out = Mat2D{
            m.a * na + m.c * nb, m.b * na + m.d * nb,
            m.a * nc + m.c * nd, m.b * nc + m.d * nd,
            m.a * ne + m.c * nf + m.e, m.b * ne + m.d * nf + m.f,
        };
    }
    return out;
}

// Apply one Mat2D to a point.
Vec2 apply_point(const Mat2D& m, Vec2 p) {
    return Vec2{m.a * p.x + m.c * p.y + m.e, m.b * p.x + m.d * p.y + m.f};
}

// Extents (also compute via path bbox).
template <typename T>
Vec2 apply_mat_vec(T fn_compute_bbox) { return {0, 0}; } // (unused)

// ─── DOM walker ────────────────────────────────────────────────────────

struct Cursor {
    std::string_view text;
    std::size_t      i = 0;
    explicit Cursor(std::string_view t) : text(t), i(0) {}
    bool eof() const { return i >= text.size(); }
    void skip_ws() { while (!eof() && std::isspace(static_cast<unsigned char>(text[i]))) ++i; }
    char peek() const { return eof() ? '\0' : text[i]; }
    char get() { return eof() ? '\0' : text[i++]; }
};

bool find_tag(Cursor& c, const std::string& tag, Attrs& out_attrs, bool& self_close) {
    c.skip_ws();
    if (c.eof() || c.peek() != '<') return false;
    c.get(); // consume '<'
    if (c.peek() == '/' || c.peek() == '!') {
        // skip CDATA / comments / closing tag (closure handled at call-site)
        return false;
    }
    // Read element name
    std::string name;
    while (!c.eof() && std::isalpha(static_cast<unsigned char>(c.peek()))) name += c.get();
    if (to_lower(name) != tag) {
        // We only handle explicit tag-name requests; nothing useful to do at top-level
        // until we hit a known tag.  Skip ahead to next '<'.
        std::size_t next_lt = c.text().find('<', c.i);
        c.i = (next_lt == std::string::npos) ? c.text().size() : next_lt;
        return false;
    }
    c.skip_ws();
    self_close = false;
    // Read attribute string up to '>'.
    std::size_t start = c.i;
    while (!c.eof() && c.peek() != '>') ++c.i;
    std::string attr_block(start, c.text().data() + c.i, c.i - start);
    out_attrs = parse_attrs(attr_block);
    if (!c.eof() && c.peek() == '>') {
        // check if self-closing
        self_close = (c.i > 0 && c.text[c.i - 1] == '/');
        c.get();
    }
    return true;
}

// Find a closing tag </tag> in the remaining text; returns position just after '>'.
std::size_t find_close(Cursor& c, const std::string& tag) {
    std::string needle = "</" + tag + ">";
    auto it = c.text().find(needle, c.i);
    return it == std::string::npos ? std::string::npos : it + needle.size();
}

// Walk content between <element> and matching close, emitting descriptors
// of child elements (which become SvgElement after `apply` to runtime).
void walk_children(std::string_view body,
                   const Mat2D& current_matrix,
                   std::vector<SvgElement>& out,
                   std::vector<SvgImportError>& errors) {
    Cursor c(body);
    while (!c.eof()) {
        c.skip_ws();
        if (c.eof()) break;
        if (c.peek() != '<') { c.get(); continue; }
        if (c.peek() == '/' || c.peek() == '!') {
            // skip until '>'
            while (!c.eof() && c.get() != '>') {}
            continue;
        }
        // Parse a starting tag.
        std::size_t start_pos = c.i;
        std::string tag_name;
        while (!c.eof() && std::isalpha(static_cast<unsigned char>(c.peek()))) tag_name += c.get();
        tag_name = to_lower(tag_name);

        // Read the rest of the tag into attr string.
        std::size_t a_start = c.i;
        while (!c.eof() && c.peek() != '>') ++c.i;
        std::string attr_block(a_start, c.text().data() + c.i, c.i - a_start);
        bool self_close = false;
        if (!c.eof() && c.peek() == '>') {
            self_close = (c.i > 0 && c.text[c.i - 1] == '/');
            c.get();
        }
        Attrs attrs = parse_attrs(attr_block);

        if (tag_name == "g") {
            // Apply transform and recurse.
            Mat2D m = current_matrix;
            if (attrs.count("transform")) {
                std::string tf = attrs["transform"];
                // transform may contain multiple functions separated by spaces.
                std::stringstream ss(tf);
                std::string part;
                while (ss >> part) m = apply(m, part);
            }
            std::size_t close = find_close(c, "g");
            std::string_view inner;
            if (close != std::string::npos) {
                inner = std::string_view(c.text().data() + c.i, close - 2 - c.i);
            } else {
                errors.push_back({"<g> never closed", start_pos});
                inner = std::string_view(c.text().data() + c.i);
            }
            walk_children(inner, m, out, errors);
            if (close != std::string::npos) c.i = close;
            continue;
        }

        if (tag_name == "path") {
            SvgElement el;
            el.id = attrs.count("id") ? attrs["id"] : "";
            el.kind = SvgElement::Kind::Path;
            el.path_d = attrs.count("d") ? attrs["d"] : "";
            if (el.path_d.empty()) {
                errors.push_back({"<path> missing d attribute", start_pos});
            } else {
                auto res = chronon3d::assets::parse_svg_path_data(el.path_d);
                if (res.ok) {
                    el.bbox_min = apply_point(current_matrix, res.path().commands.front().p0);
                    el.bbox_max = el.bbox_min;
                    for (const auto& cmd : res.path().commands) {
                        Vec2 p = apply_point(current_matrix, cmd.p0);
                        el.bbox_min.x = std::min(el.bbox_min.x, p.x);
                        el.bbox_min.y = std::min(el.bbox_min.y, p.y);
                        el.bbox_max.x = std::max(el.bbox_max.x, p.x);
                        el.bbox_max.y = std::max(el.bbox_max.y, p.y);
                    }
                    if (attrs.count("fill") && parse_color(attrs["fill"], el.fill))   el.has_fill   = true;
                    if (attrs.count("stroke") && parse_color(attrs["stroke"], el.stroke)) el.has_stroke = true;
                    if (attrs.count("stroke-width")) el.stroke_width = parse_float(attrs["stroke-width"], 1.0f);
                    out.push_back(el);
                } else {
                    errors.push_back({"<path d> parse error: " + res.error, start_pos});
                }
            }
            continue;
        }

        if (tag_name == "rect") {
            float x = parse_float(attrs["x"]);
            float y = parse_float(attrs["y"]);
            float w = parse_float(attrs["width"]);
            float h = parse_float(attrs["height"]);
            SvgElement el;
            el.id = attrs.count("id") ? attrs["id"] : "";
            el.kind = SvgElement::Kind::Rect;
            el.rect_size = {w, h};
            el.bbox_min = apply_point(current_matrix, {x, y});
            el.bbox_max = apply_point(current_matrix, {x + w, y + h});
            if (attrs.count("fill") && parse_color(attrs["fill"], el.fill))       el.has_fill   = true;
            if (attrs.count("stroke") && parse_color(attrs["stroke"], el.stroke)) el.has_stroke = true;
            if (attrs.count("stroke-width")) el.stroke_width = parse_float(attrs["stroke-width"], 1.0f);
            out.push_back(el);
            continue;
        }

        if (tag_name == "circle") {
            float cx = parse_float(attrs["cx"]);
            float cy = parse_float(attrs["cy"]);
            float r  = parse_float(attrs["r"]);
            SvgElement el;
            el.id = attrs.count("id") ? attrs["id"] : "";
            el.kind = SvgElement::Kind::Circle;
            el.circle_pos = {cx, cy};
            el.circle_r   = r;
            el.bbox_min = apply_point(current_matrix, {cx - r, cy - r});
            el.bbox_max = apply_point(current_matrix, {cx + r, cy + r});
            if (attrs.count("fill") && parse_color(attrs["fill"], el.fill))       el.has_fill   = true;
            if (attrs.count("stroke") && parse_color(attrs["stroke"], el.stroke)) el.has_stroke = true;
            if (attrs.count("stroke-width")) el.stroke_width = parse_float(attrs["stroke-width"], 1.0f);
            out.push_back(el);
            continue;
        }

        // unknown / ignored: try to skip its body if container-shaped
        if (!self_close) {
            std::size_t close = find_close(c, tag_name);
            if (close != std::string::npos) c.i = close;
        }
    }
}

} // namespace

// ─── Public API ────────────────────────────────────────────────────────

SvgSceneImporter& SvgSceneImporter::instance() {
    static SvgSceneImporter s; return s;
}

SvgImportResult SvgSceneImporter::parse_text(std::string_view svg) const {
    SvgImportResult res;
    auto scene = std::make_shared<SvgScene>();

    // Find <svg ... > and viewBox.
    Cursor c(svg);
    c.skip_ws();
    bool self_close = false;
    Attrs svg_attrs;
    if (!find_tag(c, "svg", svg_attrs, self_close)) {
        res.errors.push_back({"Missing <svg> root element", 0});
        return res;
    }
    if (svg_attrs.count("viewBox")) {
        std::stringstream ss(svg_attrs["viewBox"]);
        float a, b, c2, d;
        if (ss >> a >> b >> c2 >> d) {
            scene->viewbox      = Vec2{a, b};
            scene->viewbox_size = Vec2{c2, d};
            scene->has_viewbox  = true;
        }
    }

    // Body: between current position and the closing </svg>.
    std::size_t close = find_close(c, "svg");
    std::string_view body;
    if (close != std::string::npos) {
        body = std::string_view(c.text().data() + c.i, close - 2 - c.i);
    } else {
        res.errors.push_back({"<svg> never closed", c.i});
        body = std::string_view(c.text().data() + c.i);
    }

    walk_children(body, Mat2D{}, scene->elements, res.errors);
    res.scene = scene;
    return res;
}

SvgImportResult SvgSceneImporter::load_file(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        SvgImportResult r; r.scene = it->second; return r;
    }

    SvgImportResult r;
    std::ifstream f(path);
    if (!f) {
        r.errors.push_back({"Cannot open file: " + path, 0});
        return r;
    }
    std::stringstream ss; ss << f.rdbuf();
    auto parsed = parse_text(ss.str());
    if (parsed.ok()) cache_[path] = parsed.scene;
    return parsed;
}

void SvgSceneImporter::apply(const SvgScene& scene,
                             chronon3d::SceneBuilder& s,
                             Vec2 origin,
                             f32 scale,
                             const std::string& id_prefix) {
    if (scene.elements.empty()) return;
    for (std::size_t i = 0; i < scene.elements.size(); ++i) {
        const auto& el = scene.elements[i];
        const std::string layer_id =
            id_prefix + "_" + (el.id.empty() ? ("el_" + std::to_string(i)) : el.id);
        // Element anchor computed from its bbox centre.
        Vec2 local_centre{
            (el.bbox_min.x + el.bbox_max.x) * 0.5f,
            (el.bbox_min.y + el.bbox_max.y) * 0.5f,
        };
        Vec2 world_centre{
            origin.x + (local_centre.x) * scale,
            origin.y + (local_centre.y) * scale,
        };
        Vec2 world_size{
            (el.bbox_max.x - el.bbox_min.x) * scale,
            (el.bbox_max.y - el.bbox_min.y) * scale,
        };

        if (el.kind == SvgElement::Kind::Rect) {
            chronon3d::RectParams rp;
            rp.size  = world_size;
            rp.color = el.has_fill ? el.fill : chronon3d::Color{1,1,1,0};
            rp.pos   = {0,0,0};
            s.layer(layer_id, [centre = world_centre, rp](chronon3d::LayerBuilder& l) {
                l.position({centre.x, centre.y, 0.0f});
                l.rect("bg", rp);
            });
        } else if (el.kind == SvgElement::Kind::Circle) {
            float r = std::max(2.0f, el.circle_r * scale);
            chronon3d::CircleParams cp;
            cp.radius = r;
            cp.color  = el.has_fill ? el.fill : chronon3d::Color{1,1,1,0};
            cp.pos    = {0,0,0};
            s.layer(layer_id, [centre = world_centre, cp](chronon3d::LayerBuilder& l) {
                l.position({centre.x, centre.y, 0.0f});
                l.circle("bg", cp);
            });
        } else if (el.kind == SvgElement::Kind::Path) {
            std::vector<chronon3d::PathCommand> cmds;
            auto res = chronon3d::assets::parse_svg_path_data(el.path_d);
            if (res.ok) {
                cmds.reserve(res.path().commands.size());
                for (const auto& c : res.path().commands) {
                    chronon3d::PathCommand cmd = c;
                    cmd.p0 = {
                        cmd.p0.x * scale,
                        cmd.p0.y * scale,
                    };
                    cmd.p1 = {
                        cmd.p1.x * scale,
                        cmd.p1.y * scale,
                    };
                    cmd.p2 = {
                        cmd.p2.x * scale,
                        cmd.p2.y * scale,
                    };
                    cmds.push_back(cmd);
                }
                chronon3d::PathParams pp;
                pp.commands = std::move(cmds);
                pp.color    = el.has_fill ? el.fill : chronon3d::Color{1,1,1,0};
                pp.pos      = {0,0,0};
                pp.stroke.enabled = el.has_stroke || !el.has_fill;
                pp.stroke.width   = el.stroke_width * scale;
                pp.stroke.color   = el.has_stroke ? el.stroke : chronon3d::Color{1,1,1,1};
                s.layer(layer_id, [centre = world_centre, pp](chronon3d::LayerBuilder& l) {
                    l.position({centre.x, centre.y, 0.0f});
                    l.path("bg", pp);
                });
            }
        }
    }
}

} // namespace chronon3d::motion_studio
