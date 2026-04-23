/*
 * Sloppy UI - Skia Vulkan application.
 * Opens a GLFW window and draws animated shapes with Skia via Vulkan swapchain.
 * Uses vk-bootstrap for Vulkan initialization.
 *
 * Build: cmake -B build -DSKIA_ROOT=/path/to/skia && cmake --build build
 * Run:   ./build/sloppy_ui
 */

#include "src/Swapchain.hpp"
#include "src/SkiaRenderer.hpp"
#include "src/PlatformInput.hpp"
#if !SLOPPY_UI_DIRECT_TO_DISPLAY
#include "src/InputProcessor.hpp"
#include <GLFW/glfw3.h>
#endif
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"

#include <VkBootstrap.h>

#include <cstdio>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include <vulkan/vulkan.h>

static constexpr uint32_t kApiVersion = VK_API_VERSION_1_1;
static constexpr int kWindowWidth = 1280;
static constexpr int kWindowHeight = 720;
static constexpr double kIdleFrameWait = 16.0 / 1000.0;
#if SLOPPY_UI_DIRECT_TO_DISPLAY
static constexpr const char* kBuildMode = "DIRECT_TO_DISPLAY";
#else
static constexpr const char* kBuildMode = "WINDOWED";
#endif

struct AppState {
#if !SLOPPY_UI_DIRECT_TO_DISPLAY
    GLFWwindow* window = nullptr;
#endif
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    Swapchain swapchain;
    SkiaRenderer skiaRenderer;
#if !SLOPPY_UI_DIRECT_TO_DISPLAY
    std::unique_ptr<InputProcessor> inputProcessor;
#endif
    uint32_t presentQueueIndex = 0;
    int width = kWindowWidth;
    int height = kWindowHeight;
    skgpu::VulkanBackendContext backendContext;
    std::unique_ptr<skgpu::VulkanExtensions> extensions;
    VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
};

static bool setup(AppState& state);
static int runRenderLoop(AppState& state);
static void shutdown(AppState& state);
#if SLOPPY_UI_DIRECT_TO_DISPLAY
static const char* physicalDeviceTypeToString(VkPhysicalDeviceType type);
static bool createDirectDisplaySurface(VkInstance instance, VkPhysicalDevice physicalDevice,
                                       VkSurfaceKHR* outSurface, int* outWidth, int* outHeight);
#endif

static void shutdown(AppState& state) {
#if !SLOPPY_UI_DIRECT_TO_DISPLAY
    state.inputProcessor.reset();
#endif
    state.swapchain.destroy();
    state.skiaRenderer.destroy();
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
#if !SLOPPY_UI_DIRECT_TO_DISPLAY
    if (state.window) {
        glfwDestroyWindow(state.window);
        state.window = nullptr;
    }
    glfwTerminate();
#endif
}

static bool setup(AppState& state) {
#if SLOPPY_UI_DIRECT_TO_DISPLAY
    state.extensions = std::make_unique<skgpu::VulkanExtensions>();

    vkb::InstanceBuilder builder;
    builder.set_headless(true);
    builder.set_app_name("sloppy_ui")
           .require_api_version(kApiVersion);
    builder.enable_extension(VK_KHR_SURFACE_EXTENSION_NAME);
    builder.enable_extension(VK_KHR_DISPLAY_EXTENSION_NAME);
#else
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW.\n");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    state.window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Sloppy UI", nullptr, nullptr);
    if (!state.window) {
        fprintf(stderr, "Failed to create GLFW window.\n");
        glfwTerminate();
        return false;
    }
    glfwSetWindowUserPointer(state.window, &state);

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    if (!glfwExts || glfwExtCount == 0) {
        fprintf(stderr, "GLFW could not get required Vulkan instance extensions.\n");
        return false;
    }

    state.extensions = std::make_unique<skgpu::VulkanExtensions>();

    vkb::InstanceBuilder builder;
    builder.set_app_name("sloppy_ui")
           .require_api_version(kApiVersion);

    for (uint32_t i = 0; i < glfwExtCount; ++i) {
        builder.enable_extension(glfwExts[i]);
    }
#endif

    auto vkbInstance = builder.build();
    if (!vkbInstance) {
        fprintf(stderr, "Failed to create instance: %s\n", vkbInstance.error().message().c_str());
        return false;
    }

    state.instance = vkbInstance->instance;
    state.backendContext.fInstance = state.instance;

#if SLOPPY_UI_DIRECT_TO_DISPLAY
    uint32_t physicalDeviceCount = 0;
    VkResult enumerateResult =
        vkEnumeratePhysicalDevices(state.instance, &physicalDeviceCount, nullptr);
    if (enumerateResult != VK_SUCCESS || physicalDeviceCount == 0) {
        fprintf(stderr, "Failed to enumerate Vulkan physical devices.\n");
        return false;
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    enumerateResult =
        vkEnumeratePhysicalDevices(state.instance, &physicalDeviceCount, physicalDevices.data());
    if (enumerateResult != VK_SUCCESS || physicalDevices.empty()) {
        fprintf(stderr, "Failed to enumerate Vulkan physical devices.\n");
        return false;
    }

    fprintf(stderr, "Detected %u Vulkan physical device(s):\n", physicalDeviceCount);
    for (uint32_t i = 0; i < physicalDeviceCount; ++i) {
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(physicalDevices[i], &props);
        uint32_t displayCount = 0;
        VkResult displayResult =
            vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevices[i], &displayCount, nullptr);
        if (displayResult == VK_SUCCESS) {
            fprintf(stderr, "  [%u] %s (%s) - displays: %u\n", i, props.deviceName,
                    physicalDeviceTypeToString(props.deviceType), displayCount);
        } else {
            fprintf(stderr, "  [%u] %s (%s) - display query failed: %d\n", i, props.deviceName,
                    physicalDeviceTypeToString(props.deviceType), displayResult);
        }
    }

    bool surfaceCreated = false;
    for (uint32_t i = 0; i < physicalDeviceCount; ++i) {
        int tryWidth = kWindowWidth;
        int tryHeight = kWindowHeight;
        VkSurfaceKHR trySurface = VK_NULL_HANDLE;
        if (createDirectDisplaySurface(state.instance, physicalDevices[i], &trySurface,
                                       &tryWidth, &tryHeight)) {
            state.surface = trySurface;
            state.width = tryWidth;
            state.height = tryHeight;
            surfaceCreated = true;
            fprintf(stderr, "Direct display selected physical device index: %u\n", i);
            break;
        }
    }

    if (!surfaceCreated) {
        fprintf(stderr,
            "No Vulkan displays found for any physical device. "
            "Check DRM permissions/session and ensure a connected display.\n");
        return false;
    }
#else
    if (glfwCreateWindowSurface(state.instance, state.window, nullptr, &state.surface) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan surface.\n");
        return false;
    }
#endif

    state.backendContext.fGetProc = [inst = state.instance](const char* name, VkInstance i, VkDevice dev) -> PFN_vkVoidFunction {
        VkInstance useInst = (i != VK_NULL_HANDLE) ? i : inst;
        if (dev != VK_NULL_HANDLE) {
            auto getDeviceProc = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(useInst, "vkGetDeviceProcAddr");
            return getDeviceProc ? getDeviceProc(dev, name) : nullptr;
        }
        return vkGetInstanceProcAddr(useInst, name);
    };

    vkb::PhysicalDeviceSelector physSelWithSurface(vkbInstance.value());
    physSelWithSurface.set_surface(state.surface);
#if SLOPPY_UI_DIRECT_TO_DISPLAY
    physSelWithSurface.require_present(true);
#endif
    auto vkbPhysicalDevice = physSelWithSurface.select();
    if (!vkbPhysicalDevice) {
        fprintf(stderr, "Failed to select physical device: %s\n", vkbPhysicalDevice.error().message().c_str());
        return false;
    }

    state.physicalDevice = vkbPhysicalDevice->physical_device;
    state.backendContext.fPhysicalDevice = state.physicalDevice;
    state.backendContext.fMaxAPIVersion = kApiVersion;

    vkb::DeviceBuilder deviceBuilder(vkbPhysicalDevice.value());
    auto vkbDevice = deviceBuilder.build();
    if (!vkbDevice) {
        fprintf(stderr, "Failed to create device: %s\n", vkbDevice.error().message().c_str());
        return false;
    }

    state.device = vkbDevice->device;
    state.backendContext.fDevice = state.device;

    state.presentQueueIndex = vkbDevice->get_queue_index(vkb::QueueType::graphics).value();
    state.backendContext.fGraphicsQueueIndex = state.presentQueueIndex;

    state.queue = vkbDevice->get_queue(vkb::QueueType::graphics).value();
    state.backendContext.fQueue = state.queue;

    VkBool32 graphicsCanPresent = VK_FALSE;
    VkResult presentSupportResult = vkGetPhysicalDeviceSurfaceSupportKHR(
        state.physicalDevice, state.presentQueueIndex, state.surface, &graphicsCanPresent);
    if (presentSupportResult != VK_SUCCESS || !graphicsCanPresent) {
        fprintf(stderr, "Graphics queue does not support present to selected surface.\n");
        return false;
    }

    state.backendContext.fVkExtensions = state.extensions.get();

    VkPhysicalDevice physicalDevice = state.physicalDevice;
    uint32_t deviceExtensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
    std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
    if (deviceExtensionCount > 0) {
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr,
                                            &deviceExtensionCount, deviceExtensions.data());
    }

    state.deviceFeatures2 = {};
    state.deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    state.deviceFeatures2.features = vkbPhysicalDevice->features;
    state.deviceFeatures2.features.robustBufferAccess = VK_FALSE;

    std::vector<std::string> deviceExtStr = vkbPhysicalDevice->get_extensions();
    std::vector<const char*> deviceExtensionNames;
    for (const auto& ext : deviceExtStr) {
        deviceExtensionNames.push_back(ext.c_str());
    }

    state.backendContext.fDeviceFeatures2 = &state.deviceFeatures2;

    SkiaRenderer::CreateInfo skiaInfo;
    skiaInfo.backendContext = &state.backendContext;
    skiaInfo.extensions = state.extensions.get();
    skiaInfo.deviceFeatures2 = &state.deviceFeatures2;
    skiaInfo.instance = state.instance;
    skiaInfo.physicalDevice = state.physicalDevice;
    skiaInfo.instanceExtensionCount = 0;
    skiaInfo.instanceExtensionNames = nullptr;
    skiaInfo.deviceExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size());
    skiaInfo.deviceExtensionNames = deviceExtensionNames.data();

    if (!state.skiaRenderer.create(skiaInfo)) {
        return false;
    }

    Swapchain::CreateInfo swapInfo;
    swapInfo.device = state.device;
    swapInfo.physicalDevice = state.physicalDevice;
    swapInfo.surface = state.surface;
    swapInfo.queue = state.queue;
    swapInfo.presentQueueIndex = state.presentQueueIndex;
    swapInfo.context = state.skiaRenderer.context();
    swapInfo.width = state.width;
    swapInfo.height = state.height;

    if (!state.swapchain.create(swapInfo)) {
        return false;
    }
    state.width = state.swapchain.width();
    state.height = state.swapchain.height();

#if !SLOPPY_UI_DIRECT_TO_DISPLAY
    state.inputProcessor = std::make_unique<InputProcessor>(&state.skiaRenderer);
    state.inputProcessor->setWindow(state.window);
    glfwSetKeyCallback(state.window, InputProcessor::keyCallback);
#endif

    return true;
}

static int runRenderLoop(AppState& state) {
#if SLOPPY_UI_DIRECT_TO_DISPLAY
    uint32_t currentImageIndex = 0;
    VkResult acquireResult;

    while (true) {
        std::this_thread::sleep_for(std::chrono::duration<double>(kIdleFrameWait));

        if (!state.swapchain.acquire(&currentImageIndex, &acquireResult)) {
            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
                break;
            }
            return 1;
        }

        SwapchainImage* img = state.swapchain.image(currentImageIndex);
        SkSurface* surf = img->fSurface.get();
        SkCanvas* canvas = surf->getCanvas();
        float t = static_cast<float>(platform::nowSeconds());

        state.skiaRenderer.draw(canvas, state.width, state.height, t);
        state.skiaRenderer.flushAndSubmit(surf, img->fRenderCompleteSemaphore, state.presentQueueIndex);

        VkResult presentResult;
        if (!state.swapchain.present(currentImageIndex, &presentResult)) {
            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
                break;
            }
            return 1;
        }
    }
    return 0;
#else
    uint32_t currentImageIndex = 0;
    VkResult acquireResult;

    while (!glfwWindowShouldClose(state.window)) {
        glfwWaitEventsTimeout(kIdleFrameWait);

        if (state.inputProcessor) {
            if (state.skiaRenderer.isScrolling()) {
                state.skiaRenderer.clearInputQueue();
            }
            std::pair<int, bool> event;
            while (state.skiaRenderer.pollInputEvent(event)) {
                state.skiaRenderer.processInputEvent(event.first, event.second);
            }
        }

        if (!state.swapchain.acquire(&currentImageIndex, &acquireResult)) {
            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
                break;
            }
            return 1;
        }

        SwapchainImage* img = state.swapchain.image(currentImageIndex);
        SkSurface* surf = img->fSurface.get();
        SkCanvas* canvas = surf->getCanvas();
        float t = (float)glfwGetTime();

        state.skiaRenderer.draw(canvas, state.width, state.height, t);
        state.skiaRenderer.flushAndSubmit(surf, img->fRenderCompleteSemaphore, state.presentQueueIndex);

        VkResult presentResult;
        if (!state.swapchain.present(currentImageIndex, &presentResult)) {
            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
                break;
            }
            return 1;
        }
    }
    return 0;
#endif
}

#if SLOPPY_UI_DIRECT_TO_DISPLAY
static const char* physicalDeviceTypeToString(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
        default: return "unknown";
    }
}

static bool createDirectDisplaySurface(VkInstance instance, VkPhysicalDevice physicalDevice,
                                       VkSurfaceKHR* outSurface, int* outWidth, int* outHeight) {
    uint32_t displayCount = 0;
    VkResult err = vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayCount, nullptr);
    if (err != VK_SUCCESS || displayCount == 0) {
        fprintf(stderr, "No Vulkan displays found for direct-to-display mode.\n");
        return false;
    }

    std::vector<VkDisplayPropertiesKHR> displays(displayCount);
    err = vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayCount, displays.data());
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkGetPhysicalDeviceDisplayPropertiesKHR failed: %d\n", err);
        return false;
    }

    const VkDisplayPropertiesKHR& chosenDisplay = displays[0];

    uint32_t modeCount = 0;
    err = vkGetDisplayModePropertiesKHR(physicalDevice, chosenDisplay.display, &modeCount, nullptr);
    if (err != VK_SUCCESS || modeCount == 0) {
        fprintf(stderr, "No display modes found for selected display.\n");
        return false;
    }

    std::vector<VkDisplayModePropertiesKHR> modes(modeCount);
    err = vkGetDisplayModePropertiesKHR(physicalDevice, chosenDisplay.display, &modeCount, modes.data());
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkGetDisplayModePropertiesKHR failed: %d\n", err);
        return false;
    }

    uint32_t modeIndex = 0;
    uint64_t bestPixels = 0;
    for (uint32_t i = 0; i < modeCount; ++i) {
        const auto& region = modes[i].parameters.visibleRegion;
        uint64_t pixels = static_cast<uint64_t>(region.width) * static_cast<uint64_t>(region.height);
        if (pixels > bestPixels) {
            bestPixels = pixels;
            modeIndex = i;
        }
    }
    const VkDisplayModePropertiesKHR& chosenMode = modes[modeIndex];

    uint32_t planeCount = 0;
    err = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &planeCount, nullptr);
    if (err != VK_SUCCESS || planeCount == 0) {
        fprintf(stderr, "No display planes found for selected device.\n");
        return false;
    }

    std::vector<VkDisplayPlanePropertiesKHR> planeProperties(planeCount);
    err = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &planeCount,
                                                       planeProperties.data());
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR failed: %d\n", err);
        return false;
    }

    uint32_t chosenPlane = UINT32_MAX;
    for (uint32_t plane = 0; plane < planeCount; ++plane) {
        uint32_t supportedDisplayCount = 0;
        err = vkGetDisplayPlaneSupportedDisplaysKHR(
            physicalDevice, plane, &supportedDisplayCount, nullptr);
        if (err != VK_SUCCESS || supportedDisplayCount == 0) {
            continue;
        }
        std::vector<VkDisplayKHR> supportedDisplays(supportedDisplayCount);
        err = vkGetDisplayPlaneSupportedDisplaysKHR(
            physicalDevice, plane, &supportedDisplayCount, supportedDisplays.data());
        if (err != VK_SUCCESS) {
            continue;
        }
        for (VkDisplayKHR display : supportedDisplays) {
            if (display == chosenDisplay.display) {
                chosenPlane = plane;
                break;
            }
        }
        if (chosenPlane != UINT32_MAX) {
            break;
        }
    }

    if (chosenPlane == UINT32_MAX) {
        fprintf(stderr, "No compatible display plane found for selected display.\n");
        return false;
    }

    VkDisplayPlaneCapabilitiesKHR planeCaps = {};
    err = vkGetDisplayPlaneCapabilitiesKHR(
        physicalDevice, chosenMode.displayMode, chosenPlane, &planeCaps);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkGetDisplayPlaneCapabilitiesKHR failed: %d\n", err);
        return false;
    }

    VkDisplaySurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.displayMode = chosenMode.displayMode;
    surfaceInfo.planeIndex = chosenPlane;
    surfaceInfo.planeStackIndex = planeProperties[chosenPlane].currentStackIndex;
    surfaceInfo.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    surfaceInfo.globalAlpha = 1.0f;
    if (planeCaps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR) {
        surfaceInfo.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    } else if (planeCaps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR) {
        surfaceInfo.alphaMode = VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
    } else if (planeCaps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR) {
        surfaceInfo.alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
    } else {
        surfaceInfo.alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
    }
    surfaceInfo.imageExtent = chosenMode.parameters.visibleRegion;

    err = vkCreateDisplayPlaneSurfaceKHR(instance, &surfaceInfo, nullptr, outSurface);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDisplayPlaneSurfaceKHR failed: %d\n", err);
        return false;
    }

    *outWidth = static_cast<int>(surfaceInfo.imageExtent.width);
    *outHeight = static_cast<int>(surfaceInfo.imageExtent.height);
    fprintf(stderr, "Direct display mode: %s (%ux%u)\n",
            chosenDisplay.displayName ? chosenDisplay.displayName : "unnamed display",
            surfaceInfo.imageExtent.width, surfaceInfo.imageExtent.height);
    return true;
}
#endif

int main(int, char**) {
    fprintf(stderr, "Build mode: %s\n", kBuildMode);
    AppState state;
    if (!setup(state)) {
        return 1;
    }
    int result = runRenderLoop(state);
    shutdown(state);

    return result;
}