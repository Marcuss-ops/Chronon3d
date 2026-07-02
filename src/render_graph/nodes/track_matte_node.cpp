// =============================================================================
// track_matte_node.cpp — TrackMatteNode::execute() implementation
//
/// Extracted from track_matte_node.hpp so the inline function can use
/// the SIMD matte kernels and the corrected canvas-relative coordinate logic.
///
/// Coordinate correction (the bug that this fixes):
///   The original code used the same local (x,y) indices for both the target
///   and matte framebuffers.  This is only correct when both have the same
///   canvas origin.  The fix converts each target pixel position to canvas
///   coordinates, then maps back to the matte's local coordinate space.
///   Pixels that fall outside the matte region use Color::transparent().
///
/// Luma correction (second bug):
///   The original luma formula was: luma(r,g,b,a) * a
///   For a premultiplied framebuffer, r = R*α, so the correct luma is:
///   0.2126*r + 0.7152*g + 0.0722*b   (the alpha is already baked in).
///   Multiplying by α again gives α², which is incorrect.
// =============================================================================

#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/compositor/matte.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <span>

namespace chronon3d::graph {

NodeExecResult TrackMatteNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> /*input_bboxes*/
) {
    if (inputs.size() < 2 || !inputs[0] || !inputs[1]) {
        if (inputs.empty() || !inputs[0]) {
            return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height)};
        }
        return NodeExecResult{ctx.acquire_owned_fb(*inputs[0])};
    }

    const Framebuffer& target = *inputs[0];
    const Framebuffer& matte  = *inputs[1];

    auto out = ctx.acquire_owned_fb(target.width(), target.height());

    const int W = target.width();
    const int H = target.height();
    const int matte_w = matte.width();
    const int matte_h = matte.height();

    // Precompute canvas-relative origin offsets.
    const int target_ox = target.origin_x();
    const int target_oy = target.origin_y();
    const int matte_ox = matte.origin_x();
    const int matte_oy = matte.origin_y();

    const bool inverted = (m_type == TrackMatteType::AlphaInverted ||
                           m_type == TrackMatteType::LumaInverted);
    const bool use_luma = (m_type == TrackMatteType::Luma ||
                           m_type == TrackMatteType::LumaInverted);

    for (int y = 0; y < H; ++y) {
        // Canvas Y for this row.
        const int canvas_y = target_oy + y;

        Color* out_row = out->pixels_row(y);
        const Color* target_row = target.pixels_row(y);

        // Map canvas_y to matte local y.
        const int matte_local_y = canvas_y - matte_oy;

        // If the entire row is outside the matte:
        if (matte_local_y < 0 || matte_local_y >= matte_h) {
            if (inverted) {
                // For inverted modes, outside matte = full coverage (coverage = 1).
                std::copy(target_row, target_row + W, out_row);
            } else {
                // For non-inverted modes, outside matte = transparent.
                std::fill(out_row, out_row + W, Color::transparent());
            }
            continue;
        }

        const Color* matte_row = matte.pixels_row(matte_local_y);

        // Process this row with pixel-level canvas-relative coordinate mapping.
        // We batch contiguous visible segments for efficiency.
        int seg_start = 0;
        while (seg_start < W) {
            // Find a contiguous segment where target x maps to a valid matte x.
            int seg_end = seg_start;
            while (seg_end < W) {
                const int matte_local_x = (target_ox + seg_end) - matte_ox;
                if (matte_local_x < 0 || matte_local_x >= matte_w) {
                    // This pixel is outside the matte.
                    // Break the segment before this pixel.
                    break;
                }
                ++seg_end;
            }

            if (seg_end > seg_start) {
                // Segment is within matte bounds — use SIMD or fallback.
                const int seg_len = seg_end - seg_start;
                const int matte_x_start = (target_ox + seg_start) - matte_ox;

                // Copy target pixels to output for this segment first.
                std::copy(target_row + seg_start,
                          target_row + seg_start + seg_len,
                          out_row + seg_start);

                // Apply matte coverage via SIMD kernels.
                if (use_luma) {
                    simd::apply_luma_matte_premul(
                        std::span<Color>(out_row + seg_start, seg_len),
                        std::span<const Color>(matte_row + matte_x_start, seg_len),
                        inverted);
                } else {
                    simd::apply_alpha_matte_premul(
                        std::span<Color>(out_row + seg_start, seg_len),
                        std::span<const Color>(matte_row + matte_x_start, seg_len),
                        inverted);
                }
            }

            // Handle pixels outside the matte (and advance past them).
            int gap_start = seg_end;
            int gap_end = gap_start;
            while (gap_end < W) {
                const int matte_local_x = (target_ox + gap_end) - matte_ox;
                if (matte_local_x >= 0 && matte_local_x < matte_w) {
                    break;
                }
                ++gap_end;
            }

            if (gap_end > gap_start) {
                // Outside matte: for inverted → full coverage, else transparent.
                if (inverted) {
                    std::copy(target_row + gap_start,
                              target_row + gap_start + (gap_end - gap_start),
                              out_row + gap_start);
                } else {
                    std::fill(out_row + gap_start,
                              out_row + gap_start + (gap_end - gap_start),
                              Color::transparent());
                }
            }

            seg_start = gap_end;
        }
    }

    out->set_opaque(false);
    return NodeExecResult{std::move(out)};
}

} // namespace chronon3d::graph
