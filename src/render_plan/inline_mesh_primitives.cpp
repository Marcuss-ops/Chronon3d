#include <chronon3d/render_plan/inline_mesh.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace chronon3d::render_plan {
namespace {

void requirePositive(float value, const char* name) {
    if (!std::isfinite(value) || value <= 0.f)
        throw std::invalid_argument(std::string("inline mesh primitive ") + name + " must be finite and positive");
}

void requireSegments(std::uint32_t value, const char* name, std::uint32_t minimum = 1) {
    if (value < minimum || value > 512)
        throw std::invalid_argument(std::string("inline mesh primitive ") + name + " must be in [" +
                                    std::to_string(minimum) + ", 512]");
}

InlineMeshPlan allocate(std::uint64_t vertices, std::uint64_t indices) {
    if (vertices < 3 || vertices > kInlineMeshMaxVertices || indices < 3 ||
        indices > kInlineMeshMaxIndices || indices % 3 != 0)
        throw std::invalid_argument("inline mesh primitive expands beyond the vertex/index budget");
    InlineMeshPlan mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(vertices));
    mesh.indices.reserve(static_cast<std::size_t>(indices));
    return mesh;
}

using V = InlineMeshVertex;
void triangle(InlineMeshPlan& mesh, V a, V b, V c) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {a, b, c});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
}

InlineMeshPlan box(const InlineMeshPrimitivePlan& p) {
    requirePositive(p.width, "width"); requirePositive(p.height, "height"); requirePositive(p.depth, "depth");
    auto mesh = allocate(36, 36);
    const float x = p.width * .5f, y = p.height * .5f, z = p.depth * .5f;
    const auto face = [&](std::array<float,3> n, std::array<std::array<float,3>,4> q) {
        const std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
        const V a{q[0], n, uv[0]}, b{q[1], n, uv[1]}, c{q[2], n, uv[2]}, d{q[3], n, uv[3]};
        triangle(mesh, a,b,c); triangle(mesh,a,c,d);
    };
    face({0,0,1}, {{{-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z}}});
    face({0,0,-1}, {{{x,-y,-z},{-x,-y,-z},{-x,y,-z},{x,y,-z}}});
    face({1,0,0}, {{{x,-y,z},{x,-y,-z},{x,y,-z},{x,y,z}}});
    face({-1,0,0}, {{{-x,-y,-z},{-x,-y,z},{-x,y,z},{-x,y,-z}}});
    face({0,1,0}, {{{-x,y,z},{x,y,z},{x,y,-z},{-x,y,-z}}});
    face({0,-1,0}, {{{-x,-y,-z},{x,-y,-z},{x,-y,z},{-x,-y,z}}});
    return mesh;
}

InlineMeshPlan plane(const InlineMeshPrimitivePlan& p) {
    requirePositive(p.width, "width"); requirePositive(p.height, "height");
    requireSegments(p.width_segments, "width_segments"); requireSegments(p.height_segments, "height_segments");
    const std::uint64_t cols = p.width_segments + 1ULL, rows = p.height_segments + 1ULL;
    auto mesh = allocate(cols * rows, 6ULL * p.width_segments * p.height_segments);
    for (std::uint32_t iy=0; iy<=p.height_segments; ++iy) for (std::uint32_t ix=0; ix<=p.width_segments; ++ix) {
        const float u = static_cast<float>(ix)/p.width_segments, v = static_cast<float>(iy)/p.height_segments;
        mesh.vertices.push_back({{(u-.5f)*p.width,(.5f-v)*p.height,0.f},{0.f,0.f,1.f},{u,v}});
    }
    for (std::uint32_t iy=0; iy<p.height_segments; ++iy) for (std::uint32_t ix=0; ix<p.width_segments; ++ix) {
        const auto a=iy*(p.width_segments+1)+ix, b=a+1, c=a+p.width_segments+1, d=c+1;
        mesh.indices.insert(mesh.indices.end(), {a,c,b,b,c,d});
    }
    return mesh;
}

InlineMeshPlan sphere(const InlineMeshPrimitivePlan& p) {
    requirePositive(p.radius, "radius");
    requireSegments(p.width_segments, "width_segments", 3); requireSegments(p.height_segments, "height_segments", 2);
    const std::uint64_t cols=p.width_segments+1ULL, rows=p.height_segments+1ULL;
    auto mesh=allocate(cols*rows, 6ULL*p.width_segments*p.height_segments);
    constexpr float pi=3.14159265358979323846f;
    for (std::uint32_t iy=0; iy<=p.height_segments; ++iy) for (std::uint32_t ix=0; ix<=p.width_segments; ++ix) {
        const float u=static_cast<float>(ix)/p.width_segments, v=static_cast<float>(iy)/p.height_segments;
        const float phi=v*pi, theta=u*2.f*pi;
        const std::array<float,3> n{std::sin(phi)*std::cos(theta), std::cos(phi), std::sin(phi)*std::sin(theta)};
        mesh.vertices.push_back({{n[0]*p.radius,n[1]*p.radius,n[2]*p.radius},n,{u,v}});
    }
    for (std::uint32_t iy=0; iy<p.height_segments; ++iy) for (std::uint32_t ix=0; ix<p.width_segments; ++ix) {
        const auto a=iy*(p.width_segments+1)+ix,b=a+1,c=a+p.width_segments+1,d=c+1;
        if (iy != 0) mesh.indices.insert(mesh.indices.end(), {a,c,b});
        if (iy+1 != p.height_segments) mesh.indices.insert(mesh.indices.end(), {b,c,d});
    }
    return mesh;
}

InlineMeshPlan cylinder(const InlineMeshPrimitivePlan& p, bool cone) {
    requirePositive(p.height, "height");
    requireSegments(p.radial_segments, "radial_segments", 3); requireSegments(p.height_segments, "height_segments");
    const float rt=cone ? 0.f : p.radius_top, rb=p.radius_bottom;
    requirePositive(rb, "radius_bottom"); if (!cone) requirePositive(rt, "radius_top");
    const std::uint64_t sideVerts=(p.radial_segments+1ULL)*(p.height_segments+1ULL);
    const std::uint64_t capCount=cone ? 1ULL : 2ULL;
    const std::uint64_t capVerts=capCount*(p.radial_segments+2ULL);
    const std::uint64_t sideIndices=6ULL*p.radial_segments*p.height_segments;
    auto mesh=allocate(sideVerts+capVerts, sideIndices+3ULL*capCount*p.radial_segments);
    constexpr float pi=3.14159265358979323846f;
    for (std::uint32_t iy=0; iy<=p.height_segments; ++iy) {
        const float v=static_cast<float>(iy)/p.height_segments, y=(.5f-v)*p.height;
        const float r=rt+(rb-rt)*v;
        for (std::uint32_t ix=0; ix<=p.radial_segments; ++ix) {
            const float u=static_cast<float>(ix)/p.radial_segments, theta=u*2.f*pi;
            const float slope=(rb-rt)/p.height;
            const float nx=std::cos(theta), nz=std::sin(theta), inv=1.f/std::sqrt(1.f+slope*slope);
            mesh.vertices.push_back({{r*nx,y,r*nz},{nx*inv,slope*inv,nz*inv},{u,v}});
        }
    }
    for (std::uint32_t iy=0; iy<p.height_segments; ++iy) for (std::uint32_t ix=0; ix<p.radial_segments; ++ix) {
        const auto a=iy*(p.radial_segments+1)+ix,b=a+1,c=a+p.radial_segments+1,d=c+1;
        mesh.indices.insert(mesh.indices.end(), {a,b,c,b,d,c});
    }
    const auto cap=[&](float radius,float y,float ny,bool top) {
        const auto center=static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({{0,y,0},{0,ny,0},{.5f,.5f}});
        for (std::uint32_t i=0; i<=p.radial_segments; ++i) {
            const float u=static_cast<float>(i)/p.radial_segments, a=u*2.f*pi;
            mesh.vertices.push_back({{radius*std::cos(a),y,radius*std::sin(a)},{0,ny,0},{.5f+.5f*std::cos(a),.5f+.5f*std::sin(a)}});
        }
        for (std::uint32_t i=0; i<p.radial_segments; ++i) {
            const auto c=center+1+i, d=c+1;
            if (top) mesh.indices.insert(mesh.indices.end(), {center,c,d});
            else mesh.indices.insert(mesh.indices.end(), {center,d,c});
        }
    };
    if (!cone) cap(rt,p.height*.5f,1.f,true);
    cap(rb,-p.height*.5f,-1.f,false);
    return mesh;
}

InlineMeshPlan torus(const InlineMeshPrimitivePlan& p) {
    requirePositive(p.radius,"radius"); requirePositive(p.tube,"tube");
    requireSegments(p.radial_segments,"radial_segments",3); requireSegments(p.tubular_segments,"tubular_segments",3);
    const std::uint64_t cols=p.radial_segments+1ULL, rows=p.tubular_segments+1ULL;
    auto mesh=allocate(cols*rows,6ULL*p.radial_segments*p.tubular_segments);
    constexpr float pi=3.14159265358979323846f;
    for (std::uint32_t j=0;j<=p.tubular_segments;++j) for (std::uint32_t i=0;i<=p.radial_segments;++i) {
        const float u=static_cast<float>(i)/p.radial_segments*2.f*pi, v=static_cast<float>(j)/p.tubular_segments*2.f*pi;
        const float radial=p.radius+p.tube*std::cos(v);
        const std::array<float,3> n{std::cos(v)*std::cos(u),std::cos(v)*std::sin(u),std::sin(v)};
        mesh.vertices.push_back({{radial*std::cos(u),radial*std::sin(u),p.tube*std::sin(v)},n,
                                 {static_cast<float>(i)/p.radial_segments,static_cast<float>(j)/p.tubular_segments}});
    }
    for (std::uint32_t j=0;j<p.tubular_segments;++j) for (std::uint32_t i=0;i<p.radial_segments;++i) {
        const auto a=j*(p.radial_segments+1)+i,b=a+p.radial_segments+1,c=b+1,d=a+1;
        mesh.indices.insert(mesh.indices.end(),{a,b,d,b,c,d});
    }
    return mesh;
}

} // namespace

InlineMeshPlan expand_inline_mesh_primitive(const InlineMeshPrimitivePlan& p) {
    if (p.kind=="box") return box(p);
    if (p.kind=="plane") return plane(p);
    if (p.kind=="sphere") return sphere(p);
    if (p.kind=="cylinder") return cylinder(p,false);
    if (p.kind=="cone") return cylinder(p,true);
    if (p.kind=="torus") return torus(p);
    throw std::invalid_argument("inline mesh primitive kind is unsupported: " + p.kind);
}

} // namespace chronon3d::render_plan
