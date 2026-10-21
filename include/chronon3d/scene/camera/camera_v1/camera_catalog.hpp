#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_catalog.hpp
//
// Transitional custom descriptor lookup used by the legacy CameraProgram API.
//
// Replaces CameraMotionRegistry (which wraps shared_ptr<CameraMotion> and
// requires virtual dispatch + mutex + freeze).  A CameraCatalog is just an
// immutable span of NamedCameraPreset structs — no singleton, no mutex,
// no virtual calls.
//
// Custom catalogs can be constructed by the caller and passed to
// compile_camera().
//
// Migration path:
//   - PR1:  CameraCatalog exists alongside CameraMotionRegistry.
//           compile_camera() can accept an optional catalog.
//   - PR2:  All built-in presets are converted to descriptor entries.
//           register_default_camera_motions() registration was eliminated.
//   - PR7+: CameraMotionRegistry is removed; only CameraCatalog remains.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::camera_v1 {

/// A named, categorised camera preset backed by a CameraDescriptor.
struct NamedCameraPreset {
    std::string_view id;             ///< Stable dotted id, e.g. "camera.hero_push".
    std::string_view category;       ///< "push-in", "orbit", "parallax", etc.
    std::string_view description;    ///< Human-readable, single line.
    CameraDescriptor descriptor;     ///< The actual camera data.
};

/// Catalog of caller-provided descriptors. Chronon3D ships no camera presets.
class CameraCatalog {
public:
    CameraCatalog() = default;

    /// Construct from a span of caller-provided descriptors.
    explicit CameraCatalog(std::span<const NamedCameraPreset> presets)
        : presets_(presets.begin(), presets.end()) {}

    /// Find a preset by id.  Returns nullptr if not found.
    const NamedCameraPreset* find(std::string_view id) const {
        for (const auto& p : presets_) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    /// Find the descriptor for a preset id.  Returns nullptr if not found.
    const CameraDescriptor* find_descriptor(std::string_view id) const {
        auto* preset = find(id);
        return preset ? &preset->descriptor : nullptr;
    }

    /// Enumerate all preset ids.
    std::vector<std::string> ids() const {
        std::vector<std::string> result;
        result.reserve(presets_.size());
        for (const auto& p : presets_) {
            result.push_back(std::string(p.id));
        }
        return result;
    }

    /// Number of presets.
    std::size_t size() const { return presets_.size(); }
    bool empty() const { return presets_.empty(); }

    /// Access underlying span.
    std::span<const NamedCameraPreset> presets() const { return presets_; }

private:
    std::vector<NamedCameraPreset> presets_;
};

} // namespace chronon3d::camera_v1
