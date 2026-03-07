/*
 * Swapchain: Vulkan swapchain with per-image resources.
 * Manages swapchain creation, per-image fences/semaphores/surfaces, acquire and present.
 */

#pragma once

#include "SwapchainImage.hpp"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkTypes.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <cstdint>

class Swapchain {
public:
    Swapchain() = default;
    ~Swapchain() { destroy(); }

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    struct CreateInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t presentQueueIndex = 0;
        GrDirectContext* context = nullptr;
        int width = 0;
        int height = 0;
    };

    bool create(const CreateInfo& info);
    void destroy();

    bool acquire(uint32_t* outImageIndex, VkResult* outResult = nullptr);
    bool present(uint32_t imageIndex, VkResult* outResult = nullptr);

    SwapchainImage* image(uint32_t index);
    uint32_t imageCount() const { return static_cast<uint32_t>(fImages.size()); }
    int width() const { return fImages.empty() ? 0 : fImages[0].fSurface->width(); }
    int height() const { return fImages.empty() ? 0 : fImages[0].fSurface->height(); }
    VkSwapchainKHR handle() const { return fSwapchain; }

private:
    VkDevice fDevice = VK_NULL_HANDLE;
    VkQueue fQueue = VK_NULL_HANDLE;
    VkSwapchainKHR fSwapchain = VK_NULL_HANDLE;
    VkFence fAcquireFence = VK_NULL_HANDLE;
    std::vector<SwapchainImage> fImages;
    uint32_t fPresentQueueIndex = 0;
};
