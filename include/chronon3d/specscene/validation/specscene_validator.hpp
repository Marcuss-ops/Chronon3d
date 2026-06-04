#pragma once

#include <chronon3d/specscene/model/specscene.hpp>
#include <string>
#include <vector>

namespace chronon3d::specscene {

/// Validate a SpecSceneDocument for structural correctness.
///
/// Checks:
/// - Scene dimensions are positive
/// - Frame rate is > 0
/// - Duration >= 0
/// - Layer references (parent, track matte names) exist
/// - No duplicate layer names
///
/// Returns true if the document is valid.  Diagnostics are appended on failure.
[[nodiscard]] bool validate_document(
    const SpecSceneDocument& doc,
    std::vector<std::string>& diagnostics);

} // namespace chronon3d::specscene
