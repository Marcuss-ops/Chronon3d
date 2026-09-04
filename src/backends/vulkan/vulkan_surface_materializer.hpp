#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/runtime/render_surface.hpp>
#include <vulkan/vulkan.h>

#include <stdexcept>

namespace chronon3d::backends::vulkan {

// Physical Vulkan surface mechanism only.
//
// This component deliberately has no handle->slot table, next-slot cursor,
// lifetime allocator, or compiled resource table. Placement must be decided by
// the caller before reaching this boundary. The materializer only turns an
// already-selected physical image record into Vulkan resources.
template <typename ImageT, typename OwnerT>
class VulkanSurfaceMaterializer {
public:
    void set_owner(OwnerT* owner) noexcept { owner_ = owner; }

    [[nodiscard]] bool has_owner() const noexcept { return owner_ != nullptr; }

    void materialize(ImageT& image,
                     const runtime::SurfaceDesc& desc,
                     bool exportable = false) {
        require_owner();
        if (image.image != VK_NULL_HANDLE) {
            owner_->destroy_image(image);
        }
        owner_->make_image(image, desc.width, desc.height, exportable,
                           owner_->to_vk_format(desc.format));
        image.initialized = false;
        image.text_atlas_encoding = desc.text_atlas_encoding;
    }

    void destroy(ImageT& image) noexcept {
        if (owner_ == nullptr || image.image == VK_NULL_HANDLE) return;
        owner_->destroy_image(image);
    }

private:
    void require_owner() const {
        if (owner_ == nullptr) {
            throw std::logic_error("Vulkan surface materializer has no owner");
        }
    }

    OwnerT* owner_{nullptr};
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
