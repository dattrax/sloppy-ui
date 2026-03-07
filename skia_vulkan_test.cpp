/*
 * Skia Vulkan backend test.
 * Opens a GLFW window and draws animated shapes with Skia via Vulkan swapchain.
 * Uses only the public Vulkan loader, GLFW, and Skia APIs (no sk_gpu_test).
 *
 * Build: cmake -B build && cmake --build build
 * Run:   ./build/skia_vulkan_test
 */

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkColorSpace.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include "include/gpu/ganesh/vk/GrVkBackendSemaphore.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"
#include "include/gpu/vk/VulkanPreferredFeatures.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

static constexpr uint32_t kApiVersion = VK_API_VERSION_1_1;
static constexpr int kWindowWidth = 800;
static constexpr int kWindowHeight = 600;

struct SwapchainImage {
    VkSemaphore fRenderCompleteSemaphore = VK_NULL_HANDLE;
    sk_sp<SkSurface> fSurface;
};

// All state needed for rendering and teardown. setup() fills it; shutdown() cleans it.
struct AppState {
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<SwapchainImage> swapchainImages;
    VkFence acquireFence = VK_NULL_HANDLE;
    uint32_t presentQueueIndex = 0;
    int width = kWindowWidth;
    int height = kWindowHeight;
    sk_sp<GrDirectContext> context;
    SkPaint redPaint;
    SkPaint bluePaint;
    skgpu::VulkanBackendContext backendContext;
    std::unique_ptr<skgpu::VulkanExtensions> extensions;
    VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
};

// Returns true on success. On failure, cleans up state and returns false.
static bool setup(AppState& state);

// Runs the render loop until window close or error. Returns 0 on success, 1 on error.
static int runRenderLoop(AppState& state);

// Frees all resources. Safe to call on partially initialized state.
static void shutdown(AppState& state);

// --- implementation ---

static void shutdown(AppState& state) {
    if (state.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(state.device);
    }
    state.context.reset();
    for (auto& img : state.swapchainImages) {
        if (img.fRenderCompleteSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(state.device, img.fRenderCompleteSemaphore, nullptr);
            img.fRenderCompleteSemaphore = VK_NULL_HANDLE;
        }
        img.fSurface.reset();
    }
    state.swapchainImages.clear();
    if (state.acquireFence != VK_NULL_HANDLE) {
        vkDestroyFence(state.device, state.acquireFence, nullptr);
        state.acquireFence = VK_NULL_HANDLE;
    }
    if (state.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(state.device, state.swapchain, nullptr);
        state.swapchain = VK_NULL_HANDLE;
    }
    state.backendContext.fMemoryAllocator.reset();
    if (state.device != VK_NULL_HANDLE) {
        vkDestroyDevice(state.device, nullptr);
        state.device = VK_NULL_HANDLE;
    }
    if (state.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(state.instance, state.surface, nullptr);
        state.surface = VK_NULL_HANDLE;
    }
    if (state.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(state.instance, nullptr);
        state.instance = VK_NULL_HANDLE;
    }
    if (state.window) {
        glfwDestroyWindow(state.window);
        state.window = nullptr;
    }
    glfwTerminate();
}

static bool setup(AppState& state) {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW.\n");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    state.window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Skia Vulkan", nullptr, nullptr);
    if (!state.window) {
        fprintf(stderr, "Failed to create GLFW window.\n");
        glfwTerminate();
        return false;
    }

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    if (!glfwExts || glfwExtCount == 0) {
        fprintf(stderr, "GLFW could not get required Vulkan instance extensions.\n");
        shutdown(state);
        return false;
    }

    state.extensions.reset(new skgpu::VulkanExtensions());
    skgpu::VulkanPreferredFeatures skiaFeatures;
    skiaFeatures.init(kApiVersion);

    uint32_t instanceExtensionCount = 0;
    VkResult err = vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);
    if (err != VK_SUCCESS || instanceExtensionCount == 0) {
        fprintf(stderr, "Failed to enumerate instance extensions: %d\n", err);
        shutdown(state);
        return false;
    }
    std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
    err = vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data());
    if (err != VK_SUCCESS) {
        fprintf(stderr, "Failed to enumerate instance extensions: %d\n", err);
        shutdown(state);
        return false;
    }

    std::vector<const char*> instanceExtensionNames;
    skiaFeatures.addToInstanceExtensions(instanceExtensions.data(), instanceExtensions.size(),
                                         instanceExtensionNames);
    for (uint32_t i = 0; i < glfwExtCount; ++i) {
        instanceExtensionNames.push_back(glfwExts[i]);
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "skia_vulkan_test";
    appInfo.apiVersion = kApiVersion;

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &appInfo;
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensionNames.size());
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensionNames.empty() ? nullptr : instanceExtensionNames.data();

    err = vkCreateInstance(&instanceCreateInfo, nullptr, &state.instance);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed: %d\n", err);
        shutdown(state);
        return false;
    }
    state.backendContext.fInstance = state.instance;

    if (glfwCreateWindowSurface(state.instance, state.window, nullptr, &state.surface) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan surface.\n");
        shutdown(state);
        return false;
    }

    state.backendContext.fGetProc = [inst = state.instance](const char* name, VkInstance i, VkDevice dev) -> PFN_vkVoidFunction {
        VkInstance useInst = (i != VK_NULL_HANDLE) ? i : inst;
        if (dev != VK_NULL_HANDLE) {
            auto getDeviceProc = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(useInst, "vkGetDeviceProcAddr");
            return getDeviceProc ? getDeviceProc(dev, name) : nullptr;
        }
        return vkGetInstanceProcAddr(useInst, name);
    };
    skgpu::VulkanGetProc getProc = state.backendContext.fGetProc;

    uint32_t physicalDeviceCount = 0;
    err = vkEnumeratePhysicalDevices(state.instance, &physicalDeviceCount, nullptr);
    if (err != VK_SUCCESS || physicalDeviceCount == 0) {
        fprintf(stderr, "No Vulkan physical devices: %d\n", err);
        shutdown(state);
        return false;
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    err = vkEnumeratePhysicalDevices(state.instance, &physicalDeviceCount, physicalDevices.data());
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkEnumeratePhysicalDevices failed: %d\n", err);
        shutdown(state);
        return false;
    }
    VkPhysicalDevice physicalDevice = physicalDevices[0];
    state.backendContext.fPhysicalDevice = physicalDevice;

    state.backendContext.fMaxAPIVersion = kApiVersion;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    if (queueFamilyCount == 0) {
        fprintf(stderr, "No queue families.\n");
        shutdown(state);
        return false;
    }
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t graphicsQueueIndex = queueFamilyCount;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueIndex = i;
            break;
        }
    }
    if (graphicsQueueIndex == queueFamilyCount) {
        fprintf(stderr, "No graphics queue family.\n");
        shutdown(state);
        return false;
    }
    state.backendContext.fGraphicsQueueIndex = graphicsQueueIndex;

    VkBool32 canPresent = VK_FALSE;
    err = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsQueueIndex, state.surface, &canPresent);
    if (err != VK_SUCCESS || !canPresent) {
        fprintf(stderr, "Graphics queue cannot present to surface.\n");
        shutdown(state);
        return false;
    }
    state.presentQueueIndex = graphicsQueueIndex;

    uint32_t deviceExtensionCount = 0;
    err = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "Failed to enumerate device extensions: %d\n", err);
        shutdown(state);
        return false;
    }
    std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
    if (deviceExtensionCount > 0) {
        err = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr,
                                                   &deviceExtensionCount, deviceExtensions.data());
        if (err != VK_SUCCESS) {
            fprintf(stderr, "Failed to enumerate device extensions: %d\n", err);
            shutdown(state);
            return false;
        }
    }

    state.deviceFeatures2 = {};
    state.deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    skiaFeatures.addFeaturesToQuery(deviceExtensions.data(), deviceExtensions.size(), state.deviceFeatures2);
    vkGetPhysicalDeviceFeatures2(physicalDevice, &state.deviceFeatures2);
    state.deviceFeatures2.features.robustBufferAccess = VK_FALSE;

    std::vector<const char*> deviceExtensionNames;
    for (const auto& ext : deviceExtensions) {
        deviceExtensionNames.push_back(ext.extensionName);
    }
    deviceExtensionNames.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    skiaFeatures.addFeaturesToEnable(deviceExtensionNames, state.deviceFeatures2);

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensionNames.empty() ? nullptr : deviceExtensionNames.data();
    deviceCreateInfo.pNext = &state.deviceFeatures2;

    err = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &state.device);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDevice failed: %d\n", err);
        shutdown(state);
        return false;
    }
    state.backendContext.fDevice = state.device;

    vkGetDeviceQueue(state.device, graphicsQueueIndex, 0, &state.queue);
    state.backendContext.fQueue = state.queue;

    state.backendContext.fVkExtensions = state.extensions.get();
    state.backendContext.fDeviceFeatures2 = &state.deviceFeatures2;

    state.extensions->init(getProc, state.instance, physicalDevice,
                           static_cast<uint32_t>(instanceExtensionNames.size()),
                           instanceExtensionNames.data(),
                           static_cast<uint32_t>(deviceExtensionNames.size()),
                           deviceExtensionNames.data());

    state.backendContext.fMemoryAllocator =
        skgpu::VulkanMemoryAllocators::Make(state.backendContext, skgpu::ThreadSafe::kNo);

    state.context = GrDirectContexts::MakeVulkan(state.backendContext);
    if (!state.context) {
        fprintf(stderr, "Failed to create GrDirectContext (Vulkan).\n");
        shutdown(state);
        return false;
    }

    VkSurfaceCapabilitiesKHR caps;
    err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, state.surface, &caps);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "GetSurfaceCapabilities failed: %d\n", err);
        shutdown(state);
        return false;
    }
    uint32_t formatCount = 0;
    err = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, state.surface, &formatCount, nullptr);
    if (err != VK_SUCCESS || formatCount == 0) {
        fprintf(stderr, "GetSurfaceFormats failed: %d\n", err);
        shutdown(state);
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    err = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, state.surface, &formatCount, formats.data());
    if (err != VK_SUCCESS) {
        shutdown(state);
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
        extent.width = (uint32_t)state.width;
        extent.height = (uint32_t)state.height;
    }
    extent.width = std::max(caps.minImageExtent.width,
                            std::min(caps.maxImageExtent.width, extent.width));
    extent.height = std::max(caps.minImageExtent.height,
                             std::min(caps.maxImageExtent.height, extent.height));
    state.width = (int)extent.width;
    state.height = (int)extent.height;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapInfo = {};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = state.surface;
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

    err = vkCreateSwapchainKHR(state.device, &swapInfo, nullptr, &state.swapchain);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "CreateSwapchain failed: %d\n", err);
        shutdown(state);
        return false;
    }

    uint32_t swapchainImageCount = 0;
    err = vkGetSwapchainImagesKHR(state.device, state.swapchain, &swapchainImageCount, nullptr);
    if (err != VK_SUCCESS || swapchainImageCount == 0) {
        fprintf(stderr, "GetSwapchainImages failed: %d\n", err);
        shutdown(state);
        return false;
    }
    std::vector<VkImage> vkImages(swapchainImageCount);
    err = vkGetSwapchainImagesKHR(state.device, state.swapchain, &swapchainImageCount, vkImages.data());
    if (err != VK_SUCCESS) {
        shutdown(state);
        return false;
    }

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    state.swapchainImages.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        err = vkCreateSemaphore(state.device, &semInfo, nullptr, &state.swapchainImages[i].fRenderCompleteSemaphore);
        if (err != VK_SUCCESS) {
            fprintf(stderr, "CreateSemaphore failed: %d\n", err);
            shutdown(state);
            return false;
        }
        GrVkImageInfo imageInfo;
        imageInfo.fImage = vkImages[i];
        imageInfo.fImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.fFormat = chosenFormat.format;
        imageInfo.fImageUsageFlags = swapInfo.imageUsage;
        imageInfo.fSampleCount = 1;
        imageInfo.fLevelCount = 1;
        imageInfo.fCurrentQueueFamily = state.presentQueueIndex;
        imageInfo.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        GrBackendRenderTarget backendRT = GrBackendRenderTargets::MakeVk(state.width, state.height, imageInfo);
        SkSurfaceProps props(0, kRGB_H_SkPixelGeometry);
        state.swapchainImages[i].fSurface = SkSurfaces::WrapBackendRenderTarget(state.context.get(),
            backendRT, kTopLeft_GrSurfaceOrigin, swapchainColorType,
            SkColorSpace::MakeSRGB(), &props);
        if (!state.swapchainImages[i].fSurface) {
            fprintf(stderr, "WrapBackendRenderTarget failed for image %u\n", i);
            shutdown(state);
            return false;
        }
    }

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    err = vkCreateFence(state.device, &fenceInfo, nullptr, &state.acquireFence);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkCreateFence failed: %d\n", err);
        shutdown(state);
        return false;
    }

    state.redPaint.setAntiAlias(true);
    state.bluePaint.setAntiAlias(true);
    return true;
}

static int runRenderLoop(AppState& state) {
    uint32_t currentImageIndex = 0;
    VkResult err;

    while (!glfwWindowShouldClose(state.window)) {
        glfwPollEvents();

        if (vkWaitForFences(state.device, 1, &state.acquireFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            fprintf(stderr, "WaitForFences failed.\n");
            return 1;
        }
        if (vkResetFences(state.device, 1, &state.acquireFence) != VK_SUCCESS) {
            fprintf(stderr, "ResetFences failed.\n");
            return 1;
        }
        err = vkAcquireNextImageKHR(state.device, state.swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                    state.acquireFence, &currentImageIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            if (err == VK_ERROR_OUT_OF_DATE_KHR) {
                break;
            }
        } else if (err != VK_SUCCESS) {
            fprintf(stderr, "AcquireNextImage failed: %d\n", err);
            return 1;
        }
        if (vkWaitForFences(state.device, 1, &state.acquireFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            fprintf(stderr, "WaitForFences(acquire) failed.\n");
            return 1;
        }

        SkSurface* surf = state.swapchainImages[currentImageIndex].fSurface.get();
        SkCanvas* canvas = surf->getCanvas();
        float t = (float)glfwGetTime();
        canvas->clear(SK_ColorGRAY);
        float rectX = 32 + 0.5f * (1 + sinf(t)) * (state.width - 32 - 80 - 32);
        state.redPaint.setColor(SK_ColorRED);
        canvas->drawRect(SkRect::MakeXYWH(rectX, 32, 80, 80), state.redPaint);
        float circleY = 80 + 0.5f * (1 + sinf(t * 0.7f)) * (state.height - 80 - 100);
        state.bluePaint.setColor(SK_ColorBLUE);
        canvas->drawCircle(state.width * 0.5f, circleY, 50, state.bluePaint);
        float cx = state.width * 0.5f + 80 * cosf(t);
        float cy = state.height * 0.5f + 80 * sinf(t);
        SkPaint greenPaint;
        greenPaint.setColor(SK_ColorGREEN);
        greenPaint.setAntiAlias(true);
        canvas->drawCircle(cx, cy, 30, greenPaint);

        GrBackendSemaphore beSignal = GrBackendSemaphores::MakeVk(
            state.swapchainImages[currentImageIndex].fRenderCompleteSemaphore);
        GrFlushInfo flushInfo;
        flushInfo.fNumSemaphores = 1;
        flushInfo.fSignalSemaphores = &beSignal;
        skgpu::MutableTextureState presentState = skgpu::MutableTextureStates::MakeVulkan(
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, state.presentQueueIndex);
        state.context->flush(surf, flushInfo, &presentState);
        state.context->submit();

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &state.swapchainImages[currentImageIndex].fRenderCompleteSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &state.swapchain;
        presentInfo.pImageIndices = &currentImageIndex;
        err = vkQueuePresentKHR(state.queue, &presentInfo);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
            break;
        }
        if (err != VK_SUCCESS) {
            fprintf(stderr, "QueuePresent failed: %d\n", err);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    AppState state;
    if (!setup(state)) {
        return 1;
    }
    int result = runRenderLoop(state);
    shutdown(state);

    if (result == 0) {
        printf("Skia Vulkan backend test passed.\n");
    }
    return result;
}
