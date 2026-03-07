/*
 * SwapchainImage: per-image resources for Vulkan swapchain rendering.
 * Each image has its own fence (frame complete), semaphore (render complete), and Skia surface.
 * Owns full lifecycle: create() and destroy().
 */

#pragma once

#include "include/core/SkSurface.h"
#include <vulkan/vulkan_core.h>

class GrDirectContext;

class SwapchainImage {
public:
    SwapchainImage() = default;

    struct CreateInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkImage vkImage = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags imageUsage = 0;
        uint32_t presentQueueIndex = 0;
        int width = 0;
        int height = 0;
        GrDirectContext* context = nullptr;
        SkColorType colorType = kRGBA_8888_SkColorType;
    };

    bool create(const CreateInfo& info);
    void destroy();

    VkSemaphore fRenderCompleteSemaphore = VK_NULL_HANDLE;
    VkSemaphore fPresentReadySemaphore = VK_NULL_HANDLE;
    VkFence fFrameFence = VK_NULL_HANDLE;
    sk_sp<SkSurface> fSurface;

private:
    VkDevice fDevice = VK_NULL_HANDLE;
};
