/*
 * Skia Vulkan backend test.
 * Opens a GLFW window and draws animated shapes with Skia via Vulkan swapchain.
 * Uses only the public Vulkan loader, GLFW, and Skia APIs (no sk_gpu_test).
 *
 * Build: cmake -B build && cmake --build build
 * Run:   ./build/skia_vulkan_test
 */

#include "src/Swapchain.hpp"
#include "src/SkiaRenderer.hpp"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanPreferredFeatures.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

static constexpr uint32_t kApiVersion = VK_API_VERSION_1_1;
static constexpr int kWindowWidth = 1280;
static constexpr int kWindowHeight = 720;

// Vulkan/GLFW state. Skia and swapchain are in separate classes.
struct AppState {
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    Swapchain swapchain;
    SkiaRenderer skiaRenderer;
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
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

// --- implementation ---

static void shutdown(AppState& state) {
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
  state.window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Skia Vulkan", nullptr, nullptr);
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

    state.extensions.reset(new skgpu::VulkanExtensions());
    skgpu::VulkanPreferredFeatures skiaFeatures;
    skiaFeatures.init(kApiVersion);

    uint32_t instanceExtensionCount = 0;
    VkResult err = vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);
    if (err != VK_SUCCESS || instanceExtensionCount == 0) {
        fprintf(stderr, "Failed to enumerate instance extensions: %d\n", err);
        return false;
    }
    std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
    err = vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data());
    if (err != VK_SUCCESS) {
        fprintf(stderr, "Failed to enumerate instance extensions: %d\n", err);
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
        return false;
    }
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

    uint32_t physicalDeviceCount = 0;
    err = vkEnumeratePhysicalDevices(state.instance, &physicalDeviceCount, nullptr);
    if (err != VK_SUCCESS || physicalDeviceCount == 0) {
        fprintf(stderr, "No Vulkan physical devices: %d\n", err);
        return false;
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    err = vkEnumeratePhysicalDevices(state.instance, &physicalDeviceCount, physicalDevices.data());
    if (err != VK_SUCCESS) {
        fprintf(stderr, "vkEnumeratePhysicalDevices failed: %d\n", err);
        return false;
    }
    VkPhysicalDevice physicalDevice = physicalDevices[0];
    state.physicalDevice = physicalDevice;
    state.backendContext.fPhysicalDevice = physicalDevice;
    state.backendContext.fMaxAPIVersion = kApiVersion;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    if (queueFamilyCount == 0) {
        fprintf(stderr, "No queue families.\n");
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
        return false;
    }
    state.backendContext.fGraphicsQueueIndex = graphicsQueueIndex;

    VkBool32 canPresent = VK_FALSE;
    err = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsQueueIndex, state.surface, &canPresent);
    if (err != VK_SUCCESS || !canPresent) {
        fprintf(stderr, "Graphics queue cannot present to surface.\n");
        return false;
    }
    state.presentQueueIndex = graphicsQueueIndex;

    uint32_t deviceExtensionCount = 0;
    err = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "Failed to enumerate device extensions: %d\n", err);
        return false;
    }
    std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
    if (deviceExtensionCount > 0) {
        err = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr,
                                                   &deviceExtensionCount, deviceExtensions.data());
        if (err != VK_SUCCESS) {
            fprintf(stderr, "Failed to enumerate device extensions: %d\n", err);
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
        return false;
    }
    state.backendContext.fDevice = state.device;

    vkGetDeviceQueue(state.device, graphicsQueueIndex, 0, &state.queue);
    state.backendContext.fQueue = state.queue;
    state.backendContext.fVkExtensions = state.extensions.get();
    state.backendContext.fDeviceFeatures2 = &state.deviceFeatures2;

    SkiaRenderer::CreateInfo skiaInfo;
    skiaInfo.backendContext = &state.backendContext;
    skiaInfo.extensions = state.extensions.get();
    skiaInfo.deviceFeatures2 = &state.deviceFeatures2;
    skiaInfo.instance = state.instance;
    skiaInfo.physicalDevice = physicalDevice;
    skiaInfo.instanceExtensionCount = static_cast<uint32_t>(instanceExtensionNames.size());
    skiaInfo.instanceExtensionNames = instanceExtensionNames.data();
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

    glfwSetKeyCallback(state.window, keyCallback);

    return true;
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    AppState* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (action == GLFW_PRESS) {
        state->skiaRenderer.handleKey(key, true);
    }
}

static int runRenderLoop(AppState& state) {
    uint32_t currentImageIndex = 0;
    VkResult acquireResult;

    while (!glfwWindowShouldClose(state.window)) {
        glfwPollEvents();

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
