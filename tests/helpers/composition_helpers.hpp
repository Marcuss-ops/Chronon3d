#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::test_support {

inline Scene evaluate_frame(const Composition& composition, Frame frame) {
    const auto sample = SampleTime::from_frame_int(frame, composition.frame_rate());
    return composition.evaluate(make_frame_context({
        .global_time = sample,
        .duration = composition.duration(),
        .width = composition.width(),
        .height = composition.height(),
    }));
}

} // namespace chronon3d::test_support
