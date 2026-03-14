/*
 * SkiaRenderer: Skia GPU context and drawing for Vulkan swapchain surfaces.
 * Owns GrDirectContext, paint state, and provides draw/flush operations.
 */

#pragma once

#include "Movie.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <cstdint>

class SkiaRenderer {
public:
    SkiaRenderer() = default;
    ~SkiaRenderer() { destroy(); }

    SkiaRenderer(const SkiaRenderer&) = delete;
    SkiaRenderer& operator=(const SkiaRenderer&) = delete;

    struct CreateInfo {
        skgpu::VulkanBackendContext* backendContext = nullptr;
        skgpu::VulkanExtensions* extensions = nullptr;
        VkPhysicalDeviceFeatures2* deviceFeatures2 = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        uint32_t instanceExtensionCount = 0;
        const char** instanceExtensionNames = nullptr;
        uint32_t deviceExtensionCount = 0;
        const char** deviceExtensionNames = nullptr;
    };

    bool create(const CreateInfo& info);
    void destroy();

    void draw(SkCanvas* canvas, int width, int height, float time);
    bool flushAndSubmit(SkSurface* surface, VkSemaphore signalSemaphore, uint32_t presentQueueIndex);

    GrDirectContext* context() const { return fContext.get(); }

    const MovieDatabase& movies() const { return fMovies; }

    void handleKey(int key, bool pressed);

    int selectedRow() const { return fSelectedRow; }
    int selectedCol() const { return fSelectedCol; }

private:
    sk_sp<GrDirectContext> fContext;
    SkPaint fRedPaint;
    SkPaint fBluePaint;
    MovieDatabase fMovies;
    std::vector<sk_sp<SkImage>> fPosterImages;

    static constexpr int kGridCols = 4;
    static constexpr int kGridRows = 3;
    int fSelectedRow = 0;
    int fSelectedCol = 0;
};
