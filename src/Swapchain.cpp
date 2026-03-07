#include "Swapchain.hpp"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "src/gpu/GpuTypesPriv.h"
#include <cstdio>
#include <algorithm>

bool Swapchain::create(const CreateInfo& info) {
    fDevice = info.device;
    fQueue = info.queue;
    fPresentQueueIndex = info.presentQueueIndex;

    VkSurfaceCapabilitiesKHR caps;
    VkResult err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(info.physicalDevice, info.surface, &caps);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "GetSurfaceCapabilities failed: %d\n", err);
        return false;
    }

    uint32_t formatCount = 0;
    err = vkGetPhysicalDeviceSurfaceFormatsKHR(info.physicalDevice, info.surface, &formatCount, nullptr);
    if (err != VK_SUCCESS || formatCount == 0) {
        fprintf(stderr, "GetSurfaceFormats failed: %d\n", err);
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    err = vkGetPhysicalDeviceSurfaceFormatsKHR(info.physicalDevice, info.surface, &formatCount, formats.data());
    if (err != VK_SUCCESS) {
        return false;
    }

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
            chosenFormat = f;
            break;
        }
    }
    SkColorType swapchainColorType = (chosenFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
                                     ? kBGRA_8888_SkColorType : kRGBA_8888_SkColorType;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == (uint32_t)-1) {
        extent.width = (uint32_t)info.width;
        extent.height = (uint32_t)info.height;
    }
    extent.width = std::max(caps.minImageExtent.width,
                            std::min(caps.maxImageExtent.width, extent.width));
    extent.height = std::max(caps.minImageExtent.height,
                             std::min(caps.maxImageExtent.height, extent.height));

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapInfo = {};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = info.surface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = chosenFormat.format;
    swapInfo.imageColorSpace = chosenFormat.colorSpace;
    swapInfo.imageExtent = extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = caps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapInfo.clipped = VK_TRUE;

    err = vkCreateSwapchainKHR(fDevice, &swapInfo, nullptr, &fSwapchain);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "CreateSwapchain failed: %d\n", err);
        return false;
    }

    uint32_t swapchainImageCount = 0;
    err = vkGetSwapchainImagesKHR(fDevice, fSwapchain, &swapchainImageCount, nullptr);
    if (err != VK_SUCCESS || swapchainImageCount == 0) {
        fprintf(stderr, "GetSwapchainImages failed: %d\n", err);
        return false;
    }
    std::vector<VkImage> vkImages(swapchainImageCount);
    err = vkGetSwapchainImagesKHR(fDevice, fSwapchain, &swapchainImageCount, vkImages.data());
    if (err != VK_SUCCESS) {
        return false;
    }

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    err = vkCreateFence(fDevice, &fenceInfo, nullptr, &fAcquireFence);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "CreateFence(acquire) failed: %d\n", err);
        return false;
    }

    fImages.resize(swapchainImageCount);
    SwapchainImage::CreateInfo imgInfo;
    imgInfo.device = fDevice;
    imgInfo.format = chosenFormat.format;
    imgInfo.imageUsage = swapInfo.imageUsage;
    imgInfo.presentQueueIndex = info.presentQueueIndex;
    imgInfo.width = (int)extent.width;
    imgInfo.height = (int)extent.height;
    imgInfo.context = info.context;
    imgInfo.colorType = swapchainColorType;

    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        imgInfo.vkImage = vkImages[i];
        if (!fImages[i].create(imgInfo)) {
            fprintf(stderr, "SwapchainImage::create failed for image %u\n", i);
            return false;
        }
    }
    return true;
}

void Swapchain::destroy() {
    if (fDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(fDevice);
    }
    for (auto& img : fImages) {
        img.destroy();
    }
    fImages.clear();
    if (fAcquireFence != VK_NULL_HANDLE) {
        vkDestroyFence(fDevice, fAcquireFence, nullptr);
        fAcquireFence = VK_NULL_HANDLE;
    }
    if (fSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(fDevice, fSwapchain, nullptr);
        fSwapchain = VK_NULL_HANDLE;
    }
    fDevice = VK_NULL_HANDLE;
    fQueue = VK_NULL_HANDLE;
}

bool Swapchain::acquire(uint32_t* outImageIndex, VkResult* outResult) {
    auto setResult = [outResult](VkResult r) { if (outResult) *outResult = r; };

    VkResult err = vkAcquireNextImageKHR(fDevice, fSwapchain, UINT64_MAX,
                                         VK_NULL_HANDLE, fAcquireFence, outImageIndex);
    setResult(err);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (err != VK_SUCCESS) {
        fprintf(stderr, "AcquireNextImage failed: %d\n", err);
        return false;
    }

    vkWaitForFences(fDevice, 1, &fAcquireFence, VK_TRUE, UINT64_MAX);
    vkResetFences(fDevice, 1, &fAcquireFence);

    SwapchainImage& img = fImages[*outImageIndex];
    vkWaitForFences(fDevice, 1, &img.fFrameFence, VK_TRUE, UINT64_MAX);
    vkResetFences(fDevice, 1, &img.fFrameFence);

    return true;
}

bool Swapchain::present(uint32_t imageIndex, VkResult* outResult) {
    SwapchainImage& img = fImages[imageIndex];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &img.fRenderCompleteSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &img.fPresentReadySemaphore;
    VkResult err = vkQueueSubmit(fQueue, 1, &submitInfo, img.fFrameFence);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "QueueSubmit(present chain) failed: %d\n", err);
        if (outResult) *outResult = err;
        return false;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &img.fPresentReadySemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &fSwapchain;
    presentInfo.pImageIndices = &imageIndex;

    err = vkQueuePresentKHR(fQueue, &presentInfo);
    if (outResult) {
        *outResult = err;
    }
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (err != VK_SUCCESS) {
        fprintf(stderr, "QueuePresent failed: %d\n", err);
        return false;
    }
    return true;
}

SwapchainImage* Swapchain::image(uint32_t index) {
    return index < fImages.size() ? &fImages[index] : nullptr;
}
