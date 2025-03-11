#pragma once

#include <span>

// @todo: figure out a better name!
struct Dependency {
public:
    inline Dependency() = default;
    inline Dependency(std::span<Dependency> deps) {
        for (const auto& dep : deps) {
            buffer_barriers.insert(buffer_barriers.end(), dep.buffer_barriers.begin(), dep.buffer_barriers.end());
            image_barriers.insert(image_barriers.end(), dep.image_barriers.begin(), dep.image_barriers.end());
        }
    }

    inline Dependency operator+(const Dependency& other) const {
        Dependency result(*this);
        result += other;
        return result;
    }
    inline Dependency &operator+=(const Dependency& other) {
        buffer_barriers.insert(buffer_barriers.end(), other.buffer_barriers.begin(), other.buffer_barriers.end());
        image_barriers.insert(image_barriers.end(), other.image_barriers.begin(), other.image_barriers.end());
        return *this;
    }

    inline void add_buffer(VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask, VkBuffer buffer, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE) {
        buffer_barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = 0,
            .dstAccessMask = 0,
            .buffer = buffer,
            .offset = offset,
            .size = range
        });
    }
    inline void add_image(VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask, VkImage image, VkImageSubresourceRange range, VkImageLayout old_layout) {
        image_barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = 0,
            .dstAccessMask = 0,
            .oldLayout = old_layout,
            .newLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range
        });
    }

    inline void pipeline_barrier(VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask, CommandBuffer &cmd) {
        // @todo: image new layout?
        for (auto &barrier : image_barriers) {
            barrier.dstStageMask = dst_stage_mask;
            barrier.dstAccessMask = dst_access_mask;
        }

        for (auto &barrier : buffer_barriers) {
            barrier.dstStageMask = dst_stage_mask;
            barrier.dstAccessMask = dst_access_mask;
        }

        if (!buffer_barriers.empty() || !image_barriers.empty()) {
            VkDependencyInfo dependency_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = static_cast<uint32_t>(buffer_barriers.size()),
                .pBufferMemoryBarriers = buffer_barriers.data(),
                .imageMemoryBarrierCount = static_cast<uint32_t>(image_barriers.size()),
                .pImageMemoryBarriers = image_barriers.data()
            };
            vkCmdPipelineBarrier2(cmd.get(), &dependency_info);
        }
    }

    inline void clear() {
        buffer_barriers.clear();
        image_barriers.clear();
    }
private:
    std::vector<VkBufferMemoryBarrier2> buffer_barriers;
    std::vector<VkImageMemoryBarrier2> image_barriers;
};
