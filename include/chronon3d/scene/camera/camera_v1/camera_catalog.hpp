#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_catalog.hpp
//
// CameraCatalog — data-driven catalog of camera presets.
//
// Replaces CameraMotionRegistry (which wraps shared_ptr<CameraMotion> and
// requires virtual dispatch + mutex + freeze).  A CameraCatalog is just an
// immutable span of NamedCameraPreset structs — no singleton, no mutex,
// no virtual calls.
//
// For the built-in presets, call builtin_camera_presets() which returns a
// span of all bundled CameraDescriptor entries.
//
// Custom catalogs can be constructed by the caller and passed to
// compile_camera().
//
// Migration path:
//   - PR1:  CameraCatalog exists alongside CameraMotionRegistry.
//           compile_camera() can accept an optional catalog.
//   - PR2:  All built-in presets are converted to descriptor entries.
//           register_default_camera_motions() TODO is eliminated.
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

/// Catalog of named camera presets.  Can be the built-in set or a custom one.
class CameraCatalog {
public:
    CameraCatalog() = default;

    /// Construct from a span of presets (e.g. builtin_camera_presets()).
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

/// Returns a span of all built-in camera presets.
/// This is the single source of truth for bundled presets.
/// Populated as presets are migrated from CameraMotionRegistry (PR2+).
std::span<const NamedCameraPreset> builtin_camera_presets();

/// Convenience: find a built-in camera descriptor by id.
/// Equivalent to calling builtin_camera_presets() then find().
inline const CameraDescriptor* find_camera_preset(std::string_view id) {
    static const auto presets = builtin_camera_presets();
    for (const auto& p : presets) {
        if (p.id == id) return &p.descriptor;
    }
    return nullptr;
}

} // namespace chronon3d::camera_v1
