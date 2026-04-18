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
#include "src/InputProcessor.hpp"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"

#include <VkBootstrap.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

static constexpr uint32_t kApiVersion = VK_API_VERSION_1_1;
static constexpr int kWindowWidth = 1280;
static constexpr int kWindowHeight = 720;
static constexpr double kIdleFrameWait = 16.0 / 1000.0;

struct AppState {
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    Swapchain swapchain;
    SkiaRenderer skiaRenderer;
    std::unique_ptr<InputProcessor> inputProcessor;
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

static void shutdown(AppState& state) {
    state.inputProcessor.reset();
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

    auto vkbInstance = builder.build();
    if (!vkbInstance) {
        fprintf(stderr, "Failed to create instance: %s\n", vkbInstance.error().message().c_str());
        return false;
    }

    state.instance = vkbInstance->instance;
    state.backendContext.fInstance = state.instance;

    if (glfwCreateWindowSurface(state.instance, state.window, nullptr, &state.surface) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan surface.\n");
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

    vkb::PhysicalDeviceSelector physSel(vkbInstance.value());
    physSel.set_surface(state.surface);

    auto vkbPhysicalDevice = physSel.select();
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

    state.inputProcessor = std::make_unique<InputProcessor>(&state.skiaRenderer);
    state.inputProcessor->setWindow(state.window);
    glfwSetKeyCallback(state.window, InputProcessor::keyCallback);

    return true;
}

static int runRenderLoop(AppState& state) {
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
}

int main(int, char**) {
    AppState state;
    if (!setup(state)) {
        return 1;
    }
    int result = runRenderLoop(state);
    shutdown(state);

    return result;
}