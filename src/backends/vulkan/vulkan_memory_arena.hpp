#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace chronon3d::backends::vulkan {

enum class MemoryClass {
    DeviceLocal,
    HostVisible,
    ExternalExportable
};

struct VulkanAllocation {
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkDeviceSize offset{0};
    VkDeviceSize size{0};
    std::uint32_t memory_type_index{0};
    std::uint64_t block_id{0};
    bool is_dedicated{false};
};

class VulkanMemoryArena {
public:
    struct Block {
        std::uint64_t id{0};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize total_size{0};
        VkDeviceSize used_size{0};
        std::uint32_t memory_type_index{0};
        bool exportable{false};
        
        struct Chunk {
            VkDeviceSize offset{0};
            VkDeviceSize size{0};
            bool free{true};
        };
        std::vector<Chunk> chunks;
    };

    explicit VulkanMemoryArena(VkDevice device = VK_NULL_HANDLE, VkPhysicalDevice physical_device = VK_NULL_HANDLE, VkDeviceSize default_block_size = 128 * 1024 * 1024)
        : m_device(device), m_physical_device(physical_device), m_default_block_size(default_block_size) {}

    void init(VkDevice device, VkPhysicalDevice physical_device, VkDeviceSize default_block_size = 128 * 1024 * 1024) {
        m_device = device;
        m_physical_device = physical_device;
        m_default_block_size = default_block_size;
    }

    ~VulkanMemoryArena() {
        destroy();
    }

    void destroy() {
        if (m_device != VK_NULL_HANDLE) {
            for (auto& block : m_blocks) {
                if (block.memory != VK_NULL_HANDLE) {
                    vkFreeMemory(m_device, block.memory, nullptr);
                    block.memory = VK_NULL_HANDLE;
                }
            }
        }
        m_blocks.clear();
    }

    [[nodiscard]] std::uint32_t find_memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags required) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(m_physical_device, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((type_bits & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags & required) == required) {
                return i;
            }
        }
        throw std::runtime_error("VulkanMemoryArena: no compatible memory type found");
    }

    VulkanAllocation allocate(
        const VkMemoryRequirements& requirements,
        MemoryClass mem_class,
        bool dedicated_allocation = false)
    {
        VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        bool exportable = false;
        if (mem_class == MemoryClass::HostVisible) {
            flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        } else if (mem_class == MemoryClass::ExternalExportable) {
            flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            exportable = true;
            dedicated_allocation = true;
        }

        const std::uint32_t mem_type_index = find_memory_type(requirements.memoryTypeBits, flags);

        // 1. Dedicated allocation path
        if (dedicated_allocation) {
            VkExportMemoryAllocateInfo export_info{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
            export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

            VkMemoryAllocateInfo alloc_info{
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                exportable ? &export_info : nullptr,
                requirements.size,
                mem_type_index
            };

            VkDeviceMemory mem = VK_NULL_HANDLE;
            if (vkAllocateMemory(m_device, &alloc_info, nullptr, &mem) != VK_SUCCESS) {
                throw std::runtime_error("VulkanMemoryArena: dedicated vkAllocateMemory failed");
            }

            const std::uint64_t b_id = ++m_next_block_id;
            Block dedicated_block{};
            dedicated_block.id = b_id;
            dedicated_block.memory = mem;
            dedicated_block.total_size = requirements.size;
            dedicated_block.used_size = requirements.size;
            dedicated_block.memory_type_index = mem_type_index;
            dedicated_block.exportable = exportable;
            dedicated_block.chunks.push_back(Block::Chunk{0, requirements.size, false});

            m_blocks.push_back(std::move(dedicated_block));

            return VulkanAllocation{
                .memory = mem,
                .offset = 0,
                .size = requirements.size,
                .memory_type_index = mem_type_index,
                .block_id = b_id,
                .is_dedicated = true
            };
        }

        // 2. Sub-allocation from pooled blocks
        const VkDeviceSize alignment = std::max<VkDeviceSize>(requirements.alignment, 256);

        for (std::size_t b = 0; b < m_blocks.size(); ++b) {
            auto& block = m_blocks[b];
            if (block.exportable || block.memory_type_index != mem_type_index) continue;

            for (std::size_t c = 0; c < block.chunks.size(); ++c) {
                auto& chunk = block.chunks[c];
                if (!chunk.free) continue;

                const VkDeviceSize aligned_offset = (chunk.offset + alignment - 1) & ~(alignment - 1);
                const VkDeviceSize padding = aligned_offset - chunk.offset;
                const VkDeviceSize required_total = padding + requirements.size;

                if (chunk.size >= required_total) {
                    const VkDeviceSize remaining_size = chunk.size - required_total;

                    if (padding > 0) {
                        chunk.size = padding;
                        chunk.free = true;

                        Block::Chunk allocated_chunk{aligned_offset, requirements.size, false};
                        block.chunks.insert(block.chunks.begin() + c + 1, allocated_chunk);

                        if (remaining_size > 0) {
                            Block::Chunk remainder_chunk{aligned_offset + requirements.size, remaining_size, true};
                            block.chunks.insert(block.chunks.begin() + c + 2, remainder_chunk);
                        }
                    } else {
                        chunk.size = requirements.size;
                        chunk.free = false;

                        if (remaining_size > 0) {
                            Block::Chunk remainder_chunk{aligned_offset + requirements.size, remaining_size, true};
                            block.chunks.insert(block.chunks.begin() + c + 1, remainder_chunk);
                        }
                    }

                    block.used_size += requirements.size;
                    return VulkanAllocation{
                        .memory = block.memory,
                        .offset = aligned_offset,
                        .size = requirements.size,
                        .memory_type_index = mem_type_index,
                        .block_id = block.id,
                        .is_dedicated = false
                    };
                }
            }
        }

        // 3. Create a new pool block
        const VkDeviceSize new_block_size = std::max(m_default_block_size, requirements.size * 2);
        VkMemoryAllocateInfo alloc_info{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            new_block_size,
            mem_type_index
        };

        VkDeviceMemory mem = VK_NULL_HANDLE;
        if (vkAllocateMemory(m_device, &alloc_info, nullptr, &mem) != VK_SUCCESS) {
            throw std::runtime_error("VulkanMemoryArena: pool block vkAllocateMemory failed");
        }

        const std::uint64_t b_id = ++m_next_block_id;
        Block new_block{};
        new_block.id = b_id;
        new_block.memory = mem;
        new_block.total_size = new_block_size;
        new_block.used_size = requirements.size;
        new_block.memory_type_index = mem_type_index;
        new_block.exportable = false;

        new_block.chunks.push_back(Block::Chunk{0, requirements.size, false});
        if (new_block_size > requirements.size) {
            new_block.chunks.push_back(Block::Chunk{requirements.size, new_block_size - requirements.size, true});
        }

        m_blocks.push_back(std::move(new_block));

        return VulkanAllocation{
            .memory = mem,
            .offset = 0,
            .size = requirements.size,
            .memory_type_index = mem_type_index,
            .block_id = b_id,
            .is_dedicated = false
        };
    }

    void free(const VulkanAllocation& alloc) {
        if (alloc.memory == VK_NULL_HANDLE || alloc.block_id == 0) return;

        auto block_it = std::find_if(m_blocks.begin(), m_blocks.end(),
            [&](const Block& b) { return b.id == alloc.block_id; });
        if (block_it == m_blocks.end()) return;
        auto& block = *block_it;

        if (alloc.is_dedicated) {
            if (block.memory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, block.memory, nullptr);
                block.memory = VK_NULL_HANDLE;
            }
            m_blocks.erase(block_it);
            return;
        }

        for (std::size_t c = 0; c < block.chunks.size(); ++c) {
            if (block.chunks[c].offset == alloc.offset && !block.chunks[c].free) {
                block.chunks[c].free = true;
                if (block.used_size >= alloc.size) {
                    block.used_size -= alloc.size;
                }
                break;
            }
        }

        // Merge adjacent free chunks
        for (std::size_t c = 0; c + 1 < block.chunks.size();) {
            if (block.chunks[c].free && block.chunks[c + 1].free) {
                block.chunks[c].size += block.chunks[c + 1].size;
                block.chunks.erase(block.chunks.begin() + c + 1);
            } else {
                ++c;
            }
        }

        // If the pool block is completely free and we have more than 1 block, free the Vulkan memory
        if (block.used_size == 0 && m_blocks.size() > 1) {
            if (block.memory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, block.memory, nullptr);
                block.memory = VK_NULL_HANDLE;
            }
            m_blocks.erase(block_it);
        }
    }

    [[nodiscard]] std::size_t block_count() const noexcept {
        return m_blocks.size();
    }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};
    VkDeviceSize m_default_block_size{128 * 1024 * 1024};
    std::uint64_t m_next_block_id{0};
    std::vector<Block> m_blocks;
};

} // namespace chronon3d::backends::vulkan
#endif
