#include <chronon3d/renderer/software/fake_extruded_text_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include "../primitive_renderer.hpp"
#include <stb_truetype.h>
#include <mapbox/earcut.hpp>
#include <fmt/format.h>
#include <chronon3d/math/constants.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mapbox { namespace util {
    template <> struct nth<0, chronon3d::Vec2> {
        inline static float get(const chronon3d::Vec2& v) { return v.x; }
    };
    template <> struct nth<1, chronon3d::Vec2> {
        inline static float get(const chronon3d::Vec2& v) { return v.y; }
    };
}}

namespace chronon3d {

static const Vec3 k_light_dir = glm::normalize(Vec3(-0.4f, 1.2f, -0.6f));
static constexpr float k_ambient = 0.20f;
static constexpr float k_diffuse = 0.80f;
static constexpr int   k_bevel_n = 6;

static float ndotl(const Vec3& n) {
    return k_ambient + k_diffuse * std::max(0.0f, glm::dot(n, k_light_dir));
}

// ── UTF-8 decoding ────────────────────────────────────────────────────────────

static std::vector<int> utf8_to_codepoints(std::string_view s) {
    std::vector<int> out;
    size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i]);
        int cp;
        if (c < 0x80) {
            cp = c; i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0F) << 12)
               | ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 6)
               | ( static_cast<unsigned char>(s[i+2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07) << 18)
               | ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 12)
               | ((static_cast<unsigned char>(s[i+2]) & 0x3F) << 6)
               | ( static_cast<unsigned char>(s[i+3]) & 0x3F);
            i += 4;
        } else {
            cp = 0xFFFD; i += 1;
        }
        out.push_back(cp);
    }
    return out;
}

// ── Point-in-polygon (ray casting) ───────────────────────────────────────────

static bool point_in_polygon(Vec2 pt, const std::vector<Vec2>& poly) {
    bool inside = false;
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > pt.y) != (poly[j].y > pt.y)) &&
            (pt.x < (poly[j].x - poly[i].x) * (pt.y - poly[i].y)
                    / (poly[j].y - poly[i].y) + poly[i].x))
            inside = !inside;
    }
    return inside;
}

// ── Sutherland-Hodgman near-plane clipping (view space, single plane z=-near) ─

struct ClipVert { Vec3 pos; Color col; };

static std::vector<ClipVert> clip_near(const std::vector<ClipVert>& pts, float near_z = 1.0f) {
    std::vector<ClipVert> out;
    const size_t n = pts.size();
    if (n == 0) return out;
    auto inside  = [=](const ClipVert& p){ return p.pos.z <= -near_z; };
    auto lerp_c  = [](Color a, Color b, float t) {
        return Color{a.r + t*(b.r-a.r), a.g + t*(b.g-a.g),
                     a.b + t*(b.b-a.b), a.a + t*(b.a-a.a)};
    };
    for (size_t i = 0; i < n; ++i) {
        const ClipVert& a = pts[i];
        const ClipVert& b = pts[(i+1) % n];
        bool a_in = inside(a), b_in = inside(b);
        if (a_in) out.push_back(a);
        if (a_in != b_in) {
            float t = (-near_z - a.pos.z) / (b.pos.z - a.pos.z);
            out.push_back({a.pos + t*(b.pos - a.pos), lerp_c(a.col, b.col, t)});
        }
    }
    return out;
}

// ── Glyph geometry cache ──────────────────────────────────────────────────────

const GlyphGeometry& FakeExtrudedTextRenderer::get_glyph(
    const std::string& font_path, float font_size,
    int codepoint, const uint8_t* font_data)
{
    std::string key = font_path + "|" + std::to_string(font_size) + "|" + std::to_string(codepoint);
    auto it = m_glyph_cache.find(key);
    if (it != m_glyph_cache.end()) return it->second;

    GlyphGeometry geom;

    stbtt_fontinfo font;
    stbtt_InitFont(&font, font_data, 0);
    const float sc_f = stbtt_ScaleForPixelHeight(&font, font_size);

    int adv, lsb;
    stbtt_GetCodepointHMetrics(&font, codepoint, &adv, &lsb);
    geom.advance = adv * sc_f;

    stbtt_vertex* verts = nullptr;
    int nv = stbtt_GetCodepointShape(&font, codepoint, &verts);

    if (nv > 0 && verts) {
        std::vector<std::vector<Vec2>> contours;
        for (int vi = 0; vi < nv; ++vi) {
            const auto& v = verts[vi];
            if (v.type == STBTT_vmove) {
                contours.emplace_back();
                contours.back().push_back({(float)v.x * sc_f, (float)v.y * sc_f});
            } else if (v.type == STBTT_vline) {
                if (!contours.empty())
                    contours.back().push_back({(float)v.x * sc_f, (float)v.y * sc_f});
            } else if (v.type == STBTT_vcurve) {
                if (contours.empty()) continue;
                Vec2 p0 = contours.back().back();
                Vec2 cp{(float)v.cx * sc_f, (float)v.cy * sc_f};
                Vec2 p1{(float)v.x  * sc_f, (float)v.y  * sc_f};
                for (int step = 1; step <= 12; ++step) {
                    float t = step / 12.0f, mt = 1.0f - t;
                    contours.back().push_back({
                        mt*mt*p0.x + 2*mt*t*cp.x + t*t*p1.x,
                        mt*mt*p0.y + 2*mt*t*cp.y + t*t*p1.y
                    });
                }
            } else if (v.type == STBTT_vcubic) {
                if (contours.empty()) continue;
                Vec2 p0 = contours.back().back();
                Vec2 c1{(float)v.cx  * sc_f, (float)v.cy  * sc_f};
                Vec2 c2{(float)v.cx1 * sc_f, (float)v.cy1 * sc_f};
                Vec2 p1{(float)v.x   * sc_f, (float)v.y   * sc_f};
                for (int step = 1; step <= 16; ++step) {
                    float t = step / 16.0f, mt = 1.0f - t;
                    contours.back().push_back({
                        mt*mt*mt*p0.x + 3*mt*mt*t*c1.x + 3*mt*t*t*c2.x + t*t*t*p1.x,
                        mt*mt*mt*p0.y + 3*mt*mt*t*c1.y + 3*mt*t*t*c2.y + t*t*t*p1.y
                    });
                }
            }
        }
        stbtt_FreeShape(&font, verts);

        auto get_area = [](const std::vector<Vec2>& c) {
            float a = 0;
            for (size_t i = 0; i < c.size(); ++i) {
                size_t j = (i + 1) % c.size();
                a += c[i].x * c[j].y - c[j].x * c[i].y;
            }
            return a * 0.5f;
        };

        // Separate outer contours (area > 0 in stbtt winding) from holes (area < 0)
        struct OuterInfo { std::vector<Vec2> contour; float area; };
        std::vector<OuterInfo> outers;
        std::vector<std::vector<Vec2>> holes;

        for (auto& c : contours) {
            if (c.size() < 3) continue;
            float a = get_area(c);
            if (a > 0)
                outers.push_back({std::move(c), a});
            else
                holes.push_back(std::move(c));
        }

        // Build one island per outer contour
        std::vector<GlyphIsland> islands(outers.size());
        for (size_t i = 0; i < outers.size(); ++i)
            islands[i].polygon.push_back(std::move(outers[i].contour));

        // Assign each hole to the smallest outer that contains its first vertex.
        // "Smallest area" = most immediate parent in nested contours.
        for (auto& hole : holes) {
            if (hole.empty()) continue;
            Vec2 test_pt = hole[0];
            int   best      = -1;
            float best_area = std::numeric_limits<float>::max();
            for (size_t i = 0; i < islands.size(); ++i) {
                if (!islands[i].polygon.empty() &&
                    point_in_polygon(test_pt, islands[i].polygon[0]) &&
                    outers[i].area < best_area)
                {
                    best_area = outers[i].area;
                    best = static_cast<int>(i);
                }
            }
            if (best >= 0)
                islands[best].polygon.push_back(std::move(hole));
            else if (!islands.empty())
                islands[0].polygon.push_back(std::move(hole));
        }

        for (auto& island : islands) {
            island.indices = mapbox::earcut<uint32_t>(island.polygon);
            for (const auto& c : island.polygon)
                island.flat_verts.insert(island.flat_verts.end(), c.begin(), c.end());
        }
        geom.islands = std::move(islands);
    }

    return m_glyph_cache.emplace(std::move(key), std::move(geom)).first->second;
}

// ── Geometry collection (all fixes applied) ───────────────────────────────────

void FakeExtrudedTextRenderer::collect_geometry(
    const RenderNode& node, const RenderState& state,
    const Camera& camera, i32 width, i32 height,
    TextRenderer& text_renderer)
{
    const auto& s = node.shape.fake_extruded_text;
    const f32 op = state.opacity;

    const CachedFont* font_entry = text_renderer.get_font(s.font_path);
    if (!font_entry) return;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_entry->data.data(), 0)) return;

    const float sc_f = stbtt_ScaleForPixelHeight(&font, s.font_size);
    const auto codepoints = utf8_to_codepoints(s.text);

    // Compute initial cursor with proper UTF-8 advance sum
    float x_cur = 0.0f;
    {
        float total_w = 0.0f;
        for (int cp : codepoints) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
            total_w += (float)adv * sc_f;
        }
        x_cur = (s.align == TextAlign::Center) ? -total_w * 0.5f :
                (s.align == TextAlign::Right)   ? -total_w : 0.0f;
    }

    const float depth_z = (float)s.depth * s.extrude_z_step;
    const float aspect   = (float)width / (float)height;
    const Mat4  proj_mat = camera.projection_matrix(aspect);
    const Mat4  view_mat = camera.view_matrix();

    // World → view space
    auto to_view = [&](const Vec3& w) -> Vec3 {
        Vec4 v = view_mat * Vec4(w, 1.0f);
        return Vec3(v.x, v.y, v.z);
    };

    // View → screen (caller must ensure vp.z < 0)
    auto view_to_screen = [&](const Vec3& vp) -> Vec2 {
        Vec4 clip = proj_mat * Vec4(vp, 1.0f);
        float inv_w = 1.0f / clip.w;
        return Vec2{(clip.x * inv_w + 1.0f) * (float)width  * 0.5f,
                    (1.0f - clip.y * inv_w)  * (float)height * 0.5f};
    };

    // Fan-triangulate a near-clipped polygon into m_tris
    auto emit_clipped = [&](const std::vector<ClipVert>& poly) {
        if (poly.size() < 3) return;
        Vec2  sp0 = view_to_screen(poly[0].pos);
        float d0  = -poly[0].pos.z;
        for (size_t k = 1; k + 1 < poly.size(); ++k) {
            Vec2  sp1 = view_to_screen(poly[k].pos);
            Vec2  sp2 = view_to_screen(poly[k+1].pos);
            float d   = (d0 + (-poly[k].pos.z) + (-poly[k+1].pos.z)) / 3.0f;
            m_tris.push_back({{sp0, sp1, sp2},
                               {poly[0].col, poly[k].col, poly[k+1].col}, d});
        }
    };

    // Add a triangle with per-vertex colors; clips against near plane if needed
    auto add_tri = [&](const Vec3 w[3], const Color c[3]) {
        Vec3 vp[3] = {to_view(w[0]), to_view(w[1]), to_view(w[2])};
        if (vp[0].z <= -1.0f && vp[1].z <= -1.0f && vp[2].z <= -1.0f) {
            Vec2  sp[3] = {view_to_screen(vp[0]), view_to_screen(vp[1]), view_to_screen(vp[2])};
            float d     = (-vp[0].z + -vp[1].z + -vp[2].z) / 3.0f;
            m_tris.push_back({{sp[0], sp[1], sp[2]}, {c[0], c[1], c[2]}, d});
            return;
        }
        emit_clipped(clip_near({{vp[0], c[0]}, {vp[1], c[1]}, {vp[2], c[2]}}));
    };

    // Add a side quad [front-p0, front-p1, back-p1, back-p0] with two edge colors.
    // Uses SideQ (gradient quad) in the fast path; clips to triangles if needed.
    auto add_side_quad = [&](const Vec3 w[4], Color ca, Color cb, float sort_depth) {
        Vec3 vp[4] = {to_view(w[0]), to_view(w[1]), to_view(w[2]), to_view(w[3])};
        bool all_ok = vp[0].z <= -1.0f && vp[1].z <= -1.0f &&
                      vp[2].z <= -1.0f && vp[3].z <= -1.0f;
        if (all_ok) {
            Vec2 sv[4];
            for (int j = 0; j < 4; ++j) sv[j] = view_to_screen(vp[j]);
            m_quads.push_back({{sv[0], sv[1], sv[2], sv[3]}, ca, cb, sort_depth});
            return;
        }
        // Vertex colors: v[0],v[1] at front (ca); v[2],v[3] at back (cb)
        emit_clipped(clip_near({{vp[0], ca}, {vp[1], ca}, {vp[2], cb}, {vp[3], cb}}));
    };

    for (int cp : codepoints) {
        const GlyphGeometry& geom = get_glyph(
            s.font_path, s.font_size, cp, font_entry->data.data());

        auto transform_pt = [&](Vec2 p, float z) -> Vec3 {
            Vec4 local{x_cur + p.x, -p.y, z, 1.0f};
            Vec4 world = state.matrix * local;
            return Vec3(world.x, world.y, world.z);
        };

        for (const auto& island : geom.islands) {
            // Y range for front-face vertical gradient
            float ymin = +1e9f, ymax = -1e9f;
            for (const auto& v : island.flat_verts) {
                ymin = std::min(ymin, v.y);
                ymax = std::max(ymax, v.y);
            }
            const float yrange = ymax - ymin + 1e-6f;

            // Shared lighting normals (constant per island since faces are planar)
            const Vec3 front_normal = glm::normalize(Vec3(state.matrix * Vec4(0,0,-1,0)));
            const Vec3 back_normal  = glm::normalize(Vec3(state.matrix * Vec4(0,0, 1,0)));
            const float front_light = ndotl(front_normal);
            const float back_light  = ndotl(back_normal);

            // Front + back faces
            for (size_t i = 0; i < island.indices.size(); i += 3) {
                // Front face (z = 0)
                {
                    Vec3  wf[3];
                    Color cf[3];
                    for (int j = 0; j < 3; ++j) {
                        const Vec2& fv = island.flat_verts[island.indices[i+j]];
                        wf[j] = transform_pt(fv, 0.0f);
                        float ty   = std::clamp((fv.y - ymin) / yrange, 0.0f, 1.0f);
                        float grad = front_light * (1.05f - 0.10f * ty);
                        Color fc   = s.front_color.with_alpha(s.front_color.a * op);
                        cf[j] = {std::min(1.0f, fc.r*grad), std::min(1.0f, fc.g*grad),
                                 std::min(1.0f, fc.b*grad), fc.a};
                    }
                    add_tri(wf, cf);
                }

                // Back face (z = depth_z, reversed winding)
                {
                    Vec3  wb[3];
                    Color cb_arr[3];
                    Color bc     = s.side_color.with_alpha(s.side_color.a * 0.5f * op);
                    Color bc_lit = {bc.r*back_light, bc.g*back_light, bc.b*back_light, bc.a};
                    for (int j = 0; j < 3; ++j) {
                        wb[j]     = transform_pt(island.flat_verts[island.indices[i+(2-j)]], depth_z);
                        cb_arr[j] = bc_lit;
                    }
                    add_tri(wb, cb_arr);
                }
            }

            // Side (extruded) faces — one quad-strip per contour edge
            for (const auto& contour : island.polygon) {
                for (size_t i = 0; i < contour.size(); ++i) {
                    const Vec2 p0 = contour[i];
                    const Vec2 p1 = contour[(i + 1) % contour.size()];

                    Vec2 edge   = p1 - p0;
                    Vec2 norm2d = {-edge.y, edge.x};
                    float nlen  = std::sqrt(norm2d.x*norm2d.x + norm2d.y*norm2d.y);
                    if (nlen < 1e-6f) continue;
                    norm2d /= nlen;

                    const Vec2  mid       = (p0 + p1) * 0.5f;
                    const float sort_d    = -to_view(transform_pt(mid, depth_z * 0.5f)).z;
                    const Vec4  n3d_local = {norm2d.x, -norm2d.y, 0.0f, 0.0f};
                    const Vec3  n3d_world = glm::normalize(Vec3(state.matrix * n3d_local));
                    const float side_l    = ndotl(n3d_world);

                    Color c_base = s.side_color.with_alpha(s.side_color.a * op);
                    Color c_lit  = {c_base.r*side_l, c_base.g*side_l,
                                    c_base.b*side_l, c_base.a};

                    auto add_ring = [&](float z0, float z1, Color ca, Color cb) {
                        Vec3 w[4] = {
                            transform_pt(p0, z0), transform_pt(p1, z0),
                            transform_pt(p1, z1), transform_pt(p0, z1)
                        };
                        add_side_quad(w, ca, cb, sort_d);
                    };

                    if (s.bevel_size > 0.01f) {
                        const float pi_half = math::half_pi;
                        for (int bi = 0; bi < k_bevel_n; ++bi) {
                            float t0 = (float)bi       / k_bevel_n;
                            float t1 = (float)(bi + 1) / k_bevel_n;
                            float z0 = s.bevel_size * (1.0f - std::cos(t0 * pi_half));
                            float z1 = s.bevel_size * (1.0f - std::cos(t1 * pi_half));
                            float sl0 = std::cos(t0 * pi_half);
                            float sl1 = std::cos(t1 * pi_half);
                            Vec3 bn0 = glm::normalize(Vec3(state.matrix *
                                Vec4(norm2d.x*sl0, -norm2d.y*sl0, -(1.0f-sl0), 0.0f)));
                            Vec3 bn1 = glm::normalize(Vec3(state.matrix *
                                Vec4(norm2d.x*sl1, -norm2d.y*sl1, -(1.0f-sl1), 0.0f)));
                            Color bc0 = {c_base.r*ndotl(bn0), c_base.g*ndotl(bn0),
                                         c_base.b*ndotl(bn0), c_base.a};
                            Color bc1 = {c_base.r*ndotl(bn1), c_base.g*ndotl(bn1),
                                         c_base.b*ndotl(bn1), c_base.a};
                            add_ring(z0, z1, bc0, bc1);
                        }
                        add_ring(s.bevel_size, depth_z, c_lit, c_lit);
                    } else {
                        add_ring(0.0f, depth_z, c_lit, c_lit);
                    }
                }
            }
        }

        x_cur += geom.advance;
    }
}

// ── Frame batching API ────────────────────────────────────────────────────────

void FakeExtrudedTextRenderer::begin_frame() {
    m_quads.clear();
    m_tris.clear();
}

void FakeExtrudedTextRenderer::flush(Framebuffer& fb) {
    struct Prim { enum Type { Quad, Tri } type; int idx; float depth; };
    std::vector<Prim> sorted;
    sorted.reserve(m_quads.size() + m_tris.size());
    for (size_t i = 0; i < m_quads.size(); ++i)
        sorted.push_back({Prim::Quad, (int)i, m_quads[i].depth});
    for (size_t i = 0; i < m_tris.size(); ++i)
        sorted.push_back({Prim::Tri,  (int)i, m_tris[i].depth});

    std::stable_sort(sorted.begin(), sorted.end(),
        [](const Prim& a, const Prim& b){ return a.depth > b.depth; });

    for (const auto& p : sorted) {
        if (p.type == Prim::Quad) {
            const auto& q = m_quads[p.idx];
            Color gc[4] = {q.c0, q.c1, q.c1, q.c0};
            renderer::fill_gradient_quad(fb, q.v, gc);
        } else {
            const auto& t = m_tris[p.idx];
            renderer::fill_gradient_triangle(fb, t.v, t.colors);
        }
    }

    m_quads.clear();
    m_tris.clear();
}

// ── Public collect / draw ─────────────────────────────────────────────────────

void FakeExtrudedTextRenderer::collect(
    Framebuffer& fb,
    const RenderNode& node,
    const RenderState& state,
    const Camera& camera,
    i32 width, i32 height,
    TextRenderer& text_renderer)
{
    const auto& s  = node.shape.fake_extruded_text;
    const auto& rt = node.fake_extruded_text_runtime;
    const f32   op = state.opacity;

    if (!rt.cam_ready) {
        // ── Screen-space fallback (no 3D camera) ─────────────────────────────
        // Draws directly to fb (no depth accumulation needed for 2D path).
        const Vec2 front_sp{s.world_pos.x, s.world_pos.y};
        Vec2 dir{s.extrude_dir.x, s.extrude_dir.y};
        const f32 dir_len = std::sqrt(dir.x*dir.x + dir.y*dir.y);

        Framebuffer front_mask(fb.width(), fb.height());
        front_mask.clear(Color::transparent());
        {
            TextShape ts; ts.text = s.text; ts.style.font_path = s.font_path;
            ts.style.size = s.font_size; ts.style.color = Color{1,1,1,1};
            ts.style.align = s.align;
            Transform tr; tr.position = Vec3(front_sp.x, front_sp.y, 0);
            text_renderer.draw_text(ts, tr, front_mask);
        }
        if (dir_len > 0.1f && s.depth > 0) {
            const f32 inv_d = 1.0f / static_cast<f32>(s.depth);
            for (int y = 0; y < fb.height(); ++y) {
                for (int x = 0; x < fb.width(); ++x) {
                    if (front_mask.get_pixel(x,y).a > 0.05f) continue;
                    bool found = false; f32 dist = 0;
                    for (int st = 1; st <= s.depth; ++st) {
                        int sx = x - static_cast<int>(dir.x*st+0.5f);
                        int sy = y - static_cast<int>(dir.y*st+0.5f);
                        if (sx<0||sx>=fb.width()||sy<0||sy>=fb.height()) break;
                        if (front_mask.get_pixel(sx,sy).a > 0.05f) {
                            dist = st * inv_d; found = true; break;
                        }
                    }
                    if (found) {
                        Color sc = s.side_color;
                        sc.r = std::max(0.0f, sc.r - 0.30f*dist);
                        sc.g = std::max(0.0f, sc.g - 0.30f*dist);
                        sc.b = std::max(0.0f, sc.b - 0.30f*dist);
                        sc.a = s.side_color.a * (1.0f - s.side_fade*dist) * op;
                        fb.set_pixel(x, y, compositor::blend(sc, fb.get_pixel(x,y), BlendMode::Normal));
                    }
                }
            }
        }
        {
            TextShape ts; ts.text = s.text; ts.style.font_path = s.font_path;
            ts.style.size = s.font_size;
            ts.style.color = Color{s.front_color.r, s.front_color.g,
                                   s.front_color.b, s.front_color.a * op};
            ts.style.align = s.align;
            Transform tr; tr.position = Vec3(front_sp.x, front_sp.y, 0); tr.opacity = op;
            text_renderer.draw_text(ts, tr, fb, &state);
        }
        if (s.highlight_opacity > 0) {
            TextShape ts; ts.text = s.text; ts.style.font_path = s.font_path;
            ts.style.size = s.font_size;
            ts.style.color = Color{1, 1, 1, s.highlight_opacity * op};
            ts.style.align = s.align;
            Transform tr; tr.position = Vec3(front_sp.x, front_sp.y - 1, 0);
            text_renderer.draw_text(ts, tr, fb, &state);
        }
        return;
    }

    // ── 3D mesh-based extrusion: accumulate into m_quads / m_tris ────────────
    collect_geometry(node, state, camera, width, height, text_renderer);
}

void FakeExtrudedTextRenderer::draw(
    Framebuffer& fb,
    const RenderNode& node,
    const RenderState& state,
    const Camera& camera,
    i32 width, i32 height,
    TextRenderer& text_renderer)
{
    begin_frame();
    collect(fb, node, state, camera, width, height, text_renderer);
    flush(fb);
}

} // namespace chronon3d
