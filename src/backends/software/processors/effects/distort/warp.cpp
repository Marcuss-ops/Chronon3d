#include "warp.hpp"
#include "../generate/fractal_noise.hpp"

#include <chronon3d/backends/software/sampling/sampler2d.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace chronon3d::renderer {
namespace {

constexpr float kGeomEpsilon = 1.0e-6f;

// Copies the framebuffer into the scratch workspace and returns a sampler over
// the pre-warp state, exactly like apply_offset's reference path.
struct WarpContext {
    chronon3d::sampling::Sampler2D sampler;
    int x0, y0, x1, y1;
    int w, h;
};

WarpContext make_warp_context(Framebuffer& fb, sampling::EdgeMode edge_mode,
                              ::chronon3d::EffectScratchResources& scratch,
                              const std::optional<raster::BBox>& clip) {
    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    scratch.ensure_size(w, h);
    Framebuffer& temp = *scratch.framebuffer;
    temp.blit(fb, 0, 0);
    return WarpContext{chronon3d::sampling::Sampler2D(temp, edge_mode),
                       x0, y0, x1, y1, w, h};
}

float cross2(Vec2 a, Vec2 b) {
    return a.x * b.y - a.y * b.x;
}

// Inverse bilinear: given the quad a (TL), b (TR), c (BR), d (BL) and a point
// p, solve for (u, v) in the unit square whose bilinear image is p.  Returns
// false when p has no preimage (fail-closed → transparent pixel).
bool inverse_bilinear(Vec2 p, Vec2 a, Vec2 b, Vec2 c, Vec2 d,
                      float& out_u, float& out_v) {
    const Vec2 e = b - a;
    const Vec2 f = d - a;
    const Vec2 g = a - b + c - d;
    const Vec2 h = p - a;
    const float k2 = cross2(g, f);
    const float k1 = cross2(e, f) + cross2(h, g);
    const float k0 = cross2(h, e);
    float v = 0.0f;
    if (std::abs(k2) < kGeomEpsilon) {
        if (std::abs(k1) < kGeomEpsilon) return false;
        v = -k0 / k1;
    } else {
        const float w = k1 * k1 - 4.0f * k0 * k2;
        if (w < 0.0f) return false;
        const float root = std::sqrt(w);
        v = (-k1 - root) / (2.0f * k2);
        if (v < 0.0f || v > 1.0f) {
            v = (-k1 + root) / (2.0f * k2);
            if (v < 0.0f || v > 1.0f) return false;
        }
    }
    float u = 0.0f;
    const float denom_u = e.x + v * g.x;
    const float denom_v = e.y + v * g.y;
    if (std::abs(denom_u) > kGeomEpsilon) u = (h.x - f.x * v) / denom_u;
    else if (std::abs(denom_v) > kGeomEpsilon) u = (h.y - f.y * v) / denom_v;
    else return false;
    if (u < 0.0f || u > 1.0f) return false;
    out_u = u;
    out_v = v;
    return true;
}

} // namespace

void apply_corner_pin(Framebuffer& fb, const CornerPinParams& params,
                      ::chronon3d::EffectScratchResources& scratch,
                      const std::optional<raster::BBox>& clip) {
    for (const Vec2 corner : {params.top_left, params.top_right,
                              params.bottom_right, params.bottom_left}) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y) ||
            std::abs(corner.x) > 4.0f || std::abs(corner.y) > 4.0f)
            throw std::invalid_argument("corner-pin coordinates must be finite and within [-4, 4]");
    }
    auto ctx = make_warp_context(fb, params.edge_mode, scratch, clip);
    const float w = static_cast<float>(ctx.w), h = static_cast<float>(ctx.h);
    const Vec2 a = {params.top_left.x * w, params.top_left.y * h};
    const Vec2 b = {params.top_right.x * w, params.top_right.y * h};
    const Vec2 c = {params.bottom_right.x * w, params.bottom_right.y * h};
    const Vec2 d = {params.bottom_left.x * w, params.bottom_left.y * h};
    for (int y = ctx.y0; y < ctx.y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = ctx.x0; x < ctx.x1; ++x) {
            float u = 0.0f, v = 0.0f;
            if (!inverse_bilinear({static_cast<float>(x) + 0.5f,
                                   static_cast<float>(y) + 0.5f},
                                  a, b, c, d, u, v)) {
                row[x] = Color::transparent();
                continue;
            }
            row[x] = ctx.sampler.bilinear(u * w, v * h);
        }
    }
}

void apply_twirl(Framebuffer& fb, const TwirlParams& params,
                 ::chronon3d::EffectScratchResources& scratch,
                 const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(params.center.x) || !std::isfinite(params.center.y) ||
        !std::isfinite(params.angle_degrees) || !std::isfinite(params.radius) ||
        !std::isfinite(params.falloff) ||
        params.radius <= 0.0f || params.radius > 2.0f ||
        params.falloff <= 0.0f || params.falloff > 4.0f ||
        std::abs(params.angle_degrees) > 3600.0f)
        throw std::invalid_argument("twirl parameters must be finite and valid");
    auto ctx = make_warp_context(fb, params.edge_mode, scratch, clip);
    const float w = static_cast<float>(ctx.w), h = static_cast<float>(ctx.h);
    const float cx = params.center.x * w, cy = params.center.y * h;
    const float radius_px = params.radius * static_cast<float>(std::min(ctx.w, ctx.h));
    const float angle_rad = params.angle_degrees * 0.01745329251994329577f;
    for (int y = ctx.y0; y < ctx.y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = ctx.x0; x < ctx.x1; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            const float distance = std::sqrt(dx * dx + dy * dy);
            float theta = 0.0f;
            if (distance < radius_px && distance > kGeomEpsilon) {
                const float t = 1.0f - distance / radius_px;
                theta = angle_rad * std::pow(t, params.falloff);
            }
            // The forward map rotates points at radius r by +theta(r); the
            // inverse is the negative rotation at the same radius.
            const float cos_t = std::cos(-theta), sin_t = std::sin(-theta);
            const float sx = dx * cos_t - dy * sin_t + cx;
            const float sy = dx * sin_t + dy * cos_t + cy;
            row[x] = ctx.sampler.bilinear(sx, sy);
        }
    }
}

void apply_bulge(Framebuffer& fb, const BulgeParams& params,
                 ::chronon3d::EffectScratchResources& scratch,
                 const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(params.center.x) || !std::isfinite(params.center.y) ||
        !std::isfinite(params.amount) || !std::isfinite(params.radius) ||
        params.amount < -1.0f || params.amount > 1.0f ||
        params.radius <= 0.0f || params.radius > 2.0f)
        throw std::invalid_argument("bulge parameters must be finite and valid");
    auto ctx = make_warp_context(fb, params.edge_mode, scratch, clip);
    const float w = static_cast<float>(ctx.w), h = static_cast<float>(ctx.h);
    const float cx = params.center.x * w, cy = params.center.y * h;
    const float radius_px = params.radius * static_cast<float>(std::min(ctx.w, ctx.h));
    for (int y = ctx.y0; y < ctx.y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = ctx.x0; x < ctx.x1; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            const float distance = std::sqrt(dx * dx + dy * dy);
            float scale = 1.0f;
            if (distance < radius_px && distance > kGeomEpsilon) {
                const float t = 1.0f - distance / radius_px;
                scale = 1.0f + params.amount * t;
            }
            const float inv = (scale > kGeomEpsilon) ? 1.0f / scale : 0.0f;
            const float sx = dx * inv + cx;
            const float sy = dy * inv + cy;
            row[x] = ctx.sampler.bilinear(sx, sy);
        }
    }
}

void apply_mesh_warp(Framebuffer& fb, const MeshWarpParams& params,
                     ::chronon3d::EffectScratchResources& scratch,
                     const std::optional<raster::BBox>& clip) {
    if (params.cols < 2 || params.cols > 32 || params.rows < 2 || params.rows > 32 ||
        !std::isfinite(params.smoothness) || params.smoothness < 0.0f ||
        params.smoothness > 1.0f ||
        params.offsets.size() !=
            static_cast<std::size_t>(params.cols) * static_cast<std::size_t>(params.rows))
        throw std::invalid_argument("mesh-warp grid dimensions and offsets are invalid");
    for (const Vec2 offset : params.offsets) {
        if (!std::isfinite(offset.x) || !std::isfinite(offset.y) ||
            std::abs(offset.x) > 4.0f || std::abs(offset.y) > 4.0f)
            throw std::invalid_argument("mesh-warp offsets must be finite and within [-4, 4]");
    }
    auto ctx = make_warp_context(fb, params.edge_mode, scratch, clip);
    const float w = static_cast<float>(ctx.w), h = static_cast<float>(ctx.h);
    const int cols = params.cols, rows = params.rows;
    const auto smooth = [s = params.smoothness](float t) {
        const float base = t * t * (3.0f - 2.0f * t);
        return t + (base - t) * s;
    };
    for (int y = ctx.y0; y < ctx.y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = ctx.x0; x < ctx.x1; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / w * static_cast<float>(cols - 1);
            const float v = (static_cast<float>(y) + 0.5f) / h * static_cast<float>(rows - 1);
            const int gx = std::clamp(static_cast<int>(std::floor(u)), 0, cols - 2);
            const int gy = std::clamp(static_cast<int>(std::floor(v)), 0, rows - 2);
            const float fu = smooth(std::clamp(u - static_cast<float>(gx), 0.0f, 1.0f));
            const float fv = smooth(std::clamp(v - static_cast<float>(gy), 0.0f, 1.0f));
            const Vec2 d00 = params.offsets[static_cast<std::size_t>(gy) * cols + gx];
            const Vec2 d10 = params.offsets[static_cast<std::size_t>(gy) * cols + gx + 1];
            const Vec2 d01 = params.offsets[static_cast<std::size_t>(gy + 1) * cols + gx];
            const Vec2 d11 = params.offsets[static_cast<std::size_t>(gy + 1) * cols + gx + 1];
            const Vec2 top = d00 + (d10 - d00) * fu;
            const Vec2 bottom = d01 + (d11 - d01) * fu;
            const Vec2 displacement = top + (bottom - top) * fv;
            const float sx = static_cast<float>(x) + 0.5f - displacement.x * w;
            const float sy = static_cast<float>(y) + 0.5f - displacement.y * h;
            row[x] = ctx.sampler.bilinear(sx, sy);
        }
    }
}

void apply_turbulent_displace(Framebuffer& fb, const TurbulentDisplaceParams& p, float time,
                              ::chronon3d::EffectScratchResources& scratch,
                              const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(p.amount)||p.amount<0||p.amount>1||!std::isfinite(p.size)||p.size<=0||
        !std::isfinite(p.evolution)||!std::isfinite(p.evolution_speed)||!std::isfinite(time)||p.complexity<1||p.complexity>16)
        throw std::invalid_argument("turbulent displace parameters are invalid");
    auto c=make_warp_context(fb,p.edge_mode,scratch,clip);
    for(int y=c.y0;y<c.y1;++y) { Color* row=fb.pixels_row(y); for(int x=c.x0;x<c.x1;++x) {
        float nx=(x+0.5f)*p.size+(p.evolution+p.evolution_speed*time), ny=(y+0.5f)*p.size;
        float vx=0,vy=0,amp=0.5f,freq=1,total=0;
        for(std::uint32_t octave=0;octave<p.complexity;++octave) {
            vx+=evaluate_smooth_value_noise(nx*freq,ny*freq,octave)*amp;
            vy+=evaluate_smooth_value_noise(nx*freq+37.2f,ny*freq-11.8f,octave+53U)*amp;
            total+=amp; amp*=0.5f; freq*=2.0f;
        }
        const float scale=p.amount*static_cast<float>(std::min(c.w,c.h));
        row[x]=c.sampler.bilinear(x+0.5f-vx/total*scale,y+0.5f-vy/total*scale);
    }}
}

void apply_wave_warp(Framebuffer& fb, const WaveWarpParams& p, float time,
                     ::chronon3d::EffectScratchResources& scratch,
                     const std::optional<raster::BBox>& clip) {
    const float len=std::sqrt(p.direction.x*p.direction.x+p.direction.y*p.direction.y);
    if(!std::isfinite(len)||len<1e-6f||!std::isfinite(p.amplitude)||!std::isfinite(p.wavelength)||p.wavelength<=0||!std::isfinite(p.speed)||!std::isfinite(p.phase)||!std::isfinite(time))
        throw std::invalid_argument("wave warp parameters are invalid");
    auto c=make_warp_context(fb,p.edge_mode,scratch,clip); const Vec2 d=p.direction/len;
    constexpr float tau=6.2831853071795864769f;
    for(int y=c.y0;y<c.y1;++y) { Color* row=fb.pixels_row(y); for(int x=c.x0;x<c.x1;++x) {
        const float px=(x+0.5f)/c.w, py=(y+0.5f)/c.h;
        const float theta=tau*(px*d.x+py*d.y)/p.wavelength+p.phase+tau*p.speed*time;
        float wave=std::sin(theta);
        switch(p.type) { case WaveWarpParams::WaveType::Triangle: wave=2.0f/std::numbers::pi_v<float>*std::asin(wave); break;
        case WaveWarpParams::WaveType::Square: wave=wave<0?-1.0f:1.0f; break;
        case WaveWarpParams::WaveType::Saw: wave=2.0f*(theta/tau-std::floor(theta/tau+0.5f)); break; default: break; }
        const float disp=wave*p.amplitude*std::min(c.w,c.h);
        row[x]=c.sampler.bilinear(x+0.5f-d.x*disp,y+0.5f-d.y*disp);
    }}
}

void apply_ripple(Framebuffer& fb, const RippleParams& p, float time,
                  ::chronon3d::EffectScratchResources& scratch,
                  const std::optional<raster::BBox>& clip) {
    if(!std::isfinite(p.center.x)||!std::isfinite(p.center.y)||!std::isfinite(p.radius)||p.radius<=0||!std::isfinite(p.width)||p.width<=0||!std::isfinite(p.wave_height)||!std::isfinite(p.wave_speed)||!std::isfinite(p.damping)||p.damping<0||!std::isfinite(time))
        throw std::invalid_argument("ripple parameters are invalid");
    auto c=make_warp_context(fb,p.edge_mode,scratch,clip); const float m=std::min(c.w,c.h), cx=p.center.x*c.w,cy=p.center.y*c.h;
    constexpr float tau=6.2831853071795864769f;
    for(int y=c.y0;y<c.y1;++y) { Color* row=fb.pixels_row(y); for(int x=c.x0;x<c.x1;++x) {
        const float dx=x+0.5f-cx,dy=y+0.5f-cy,r=std::sqrt(dx*dx+dy*dy), rn=r/m;
        const float envelope=std::max(0.0f,1.0f-rn/p.radius);
        const float disp=p.wave_height*m*std::sin(tau*rn/p.width-p.wave_speed*time)*std::exp(-p.damping*rn)*envelope;
        const float inv=r>1e-6f?1.0f/r:0.0f;
        row[x]=c.sampler.bilinear(x+0.5f-dx*inv*disp,y+0.5f-dy*inv*disp);
    }}
}

void apply_displacement_map(Framebuffer& fb,const DisplacementMapParams& p,const Framebuffer& map,
                            ::chronon3d::EffectScratchResources& scratch,
                            const std::optional<raster::BBox>& clip) {
    if(p.map_layer.empty()||!std::isfinite(p.intensity_x)||!std::isfinite(p.intensity_y)||
       std::abs(p.intensity_x)>4.0f||std::abs(p.intensity_y)>4.0f||map.width()<=0||map.height()<=0)
        throw std::invalid_argument("displacement map parameters or input surface are invalid");
    auto c=make_warp_context(fb,p.edge_mode,scratch,clip);
    sampling::Sampler2D map_sampler(map,sampling::EdgeMode::Clamp);
    const float map_origin_x=static_cast<float>(fb.origin_x()-map.origin_x());
    const float map_origin_y=static_cast<float>(fb.origin_y()-map.origin_y());
    const float scale=static_cast<float>(std::min(fb.width(),fb.height()));
    for(int y=c.y0;y<c.y1;++y) { Color* row=fb.pixels_row(y); for(int x=c.x0;x<c.x1;++x) {
        const Color m=map_sampler.bilinear(map_origin_x+x+0.5f,map_origin_y+y+0.5f);
        float channel=0.0f;
        switch(p.channel) {
            case DisplacementMapParams::Channel::Red: channel=m.r; break;
            case DisplacementMapParams::Channel::Green: channel=m.g; break;
            case DisplacementMapParams::Channel::Blue: channel=m.b; break;
            case DisplacementMapParams::Channel::Alpha: channel=m.a; break;
            case DisplacementMapParams::Channel::Luma: channel=0.2126f*m.r+0.7152f*m.g+0.0722f*m.b; break;
        }
        const float dx=(channel-0.5f)*p.intensity_x*scale;
        const float dy=(channel-0.5f)*p.intensity_y*scale;
        row[x]=c.sampler.bilinear(x+0.5f-dx,y+0.5f-dy);
    }}
}

} // namespace chronon3d::renderer
