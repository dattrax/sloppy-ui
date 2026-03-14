#include "SwapchainImage.hpp"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkColorSpace.h"
#include <cstdio>

bool SwapchainImage::create(const CreateInfo& info) {
    fDevice = info.device;

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkResult err = vkCreateSemaphore(fDevice, &semInfo, nullptr, &fRenderCompleteSemaphore);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "CreateSemaphore(render) failed: %d\n", err);
        return false;
    }
    err = vkCreateSemaphore(fDevice, &semInfo, nullptr, &fPresentReadySemaphore);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "CreateSemaphore(present) failed: %d\n", err);
        return false;
    }

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    err = vkCreateFence(fDevice, &fenceInfo, nullptr, &fFrameFence);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkCreateFence failed: %d\n", err);
        return false;
    }

    GrVkImageInfo imageInfo;
    imageInfo.fImage = info.vkImage;
    imageInfo.fImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.fFormat = info.format;
    imageInfo.fImageUsageFlags = info.imageUsage;
    imageInfo.fSampleCount = 1;
    imageInfo.fLevelCount = 1;
    imageInfo.fCurrentQueueFamily = info.presentQueueIndex;
    imageInfo.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    GrBackendRenderTarget backendRT = GrBackendRenderTargets::MakeVk(info.width, info.height, imageInfo);
    SkSurfaceProps props(0, kRGB_H_SkPixelGeometry);
    fSurface = SkSurfaces::WrapBackendRenderTarget(info.context,
        backendRT, kTopLeft_GrSurfaceOrigin, info.colorType,
        SkColorSpace::MakeSRGB(), &props);
    if (!fSurface) {
        fprintf(stderr, "WrapBackendRenderTarget failed\n");
        return false;
    }
    return true;
}

void SwapchainImage::destroy() {
    if (fDevice != VK_NULL_HANDLE) {
        if (fRenderCompleteSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(fDevice, fRenderCompleteSemaphore, nullptr);
            fRenderCompleteSemaphore = VK_NULL_HANDLE;
        }
        if (fPresentReadySemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(fDevice, fPresentReadySemaphore, nullptr);
            fPresentReadySemaphore = VK_NULL_HANDLE;
        }
        if (fFrameFence != VK_NULL_HANDLE) {
            vkDestroyFence(fDevice, fFrameFence, nullptr);
            fFrameFence = VK_NULL_HANDLE;
        }
        fDevice = VK_NULL_HANDLE;
    }
    fSurface.reset();
}
