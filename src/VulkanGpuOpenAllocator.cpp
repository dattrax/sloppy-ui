#include "VulkanGpuOpenAllocator.hpp"

#include <vk_mem_alloc.h>

namespace {

class GpuOpenVulkanMemoryAllocator final : public skgpu::VulkanMemoryAllocator {
public:
    explicit GpuOpenVulkanMemoryAllocator(const skgpu::VulkanBackendContext& backendContext) {
        fVulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        fVulkanFunctions.vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            backendContext.fGetProc("vkGetDeviceProcAddr", backendContext.fInstance, VK_NULL_HANDLE));
        fVulkanFunctions.vkGetPhysicalDeviceProperties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
                backendContext.fGetProc("vkGetPhysicalDeviceProperties",
                                        backendContext.fInstance, VK_NULL_HANDLE));
        fVulkanFunctions.vkGetPhysicalDeviceMemoryProperties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                backendContext.fGetProc("vkGetPhysicalDeviceMemoryProperties",
                                        backendContext.fInstance, VK_NULL_HANDLE));
        fVulkanFunctions.vkAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(
            backendContext.fGetProc("vkAllocateMemory", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(
            backendContext.fGetProc("vkFreeMemory", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkMapMemory = reinterpret_cast<PFN_vkMapMemory>(
            backendContext.fGetProc("vkMapMemory", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkUnmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(
            backendContext.fGetProc("vkUnmapMemory", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkFlushMappedMemoryRanges =
            reinterpret_cast<PFN_vkFlushMappedMemoryRanges>(
                backendContext.fGetProc("vkFlushMappedMemoryRanges",
                                        backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkInvalidateMappedMemoryRanges =
            reinterpret_cast<PFN_vkInvalidateMappedMemoryRanges>(
                backendContext.fGetProc("vkInvalidateMappedMemoryRanges",
                                        backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkBindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(
            backendContext.fGetProc("vkBindBufferMemory", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkBindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(
            backendContext.fGetProc("vkBindImageMemory", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkGetBufferMemoryRequirements =
            reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(
                backendContext.fGetProc("vkGetBufferMemoryRequirements",
                                        backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkGetImageMemoryRequirements =
            reinterpret_cast<PFN_vkGetImageMemoryRequirements>(
                backendContext.fGetProc("vkGetImageMemoryRequirements",
                                        backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkCreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(
            backendContext.fGetProc("vkCreateBuffer", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(
            backendContext.fGetProc("vkDestroyBuffer", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkCreateImage = reinterpret_cast<PFN_vkCreateImage>(
            backendContext.fGetProc("vkCreateImage", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkDestroyImage = reinterpret_cast<PFN_vkDestroyImage>(
            backendContext.fGetProc("vkDestroyImage", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkCmdCopyBuffer = reinterpret_cast<PFN_vkCmdCopyBuffer>(
            backendContext.fGetProc("vkCmdCopyBuffer", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkGetBufferMemoryRequirements2KHR =
            reinterpret_cast<PFN_vkGetBufferMemoryRequirements2KHR>(
                backendContext.fGetProc("vkGetBufferMemoryRequirements2",
                                        backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkGetImageMemoryRequirements2KHR =
            reinterpret_cast<PFN_vkGetImageMemoryRequirements2KHR>(
                backendContext.fGetProc("vkGetImageMemoryRequirements2",
                                        backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkBindBufferMemory2KHR = reinterpret_cast<PFN_vkBindBufferMemory2KHR>(
            backendContext.fGetProc("vkBindBufferMemory2", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkBindImageMemory2KHR = reinterpret_cast<PFN_vkBindImageMemory2KHR>(
            backendContext.fGetProc("vkBindImageMemory2", backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR =
            reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2KHR>(
                backendContext.fGetProc("vkGetPhysicalDeviceMemoryProperties2",
                                        backendContext.fInstance, VK_NULL_HANDLE));
        fVulkanFunctions.vkGetDeviceBufferMemoryRequirements =
            reinterpret_cast<PFN_vkGetDeviceBufferMemoryRequirementsKHR>(
                backendContext.fGetProc("vkGetDeviceBufferMemoryRequirements",
                                        backendContext.fInstance, backendContext.fDevice));
        fVulkanFunctions.vkGetDeviceImageMemoryRequirements =
            reinterpret_cast<PFN_vkGetDeviceImageMemoryRequirementsKHR>(
                backendContext.fGetProc("vkGetDeviceImageMemoryRequirements",
                                        backendContext.fInstance, backendContext.fDevice));

        if (!fVulkanFunctions.vkGetDeviceProcAddr ||
            !fVulkanFunctions.vkGetPhysicalDeviceProperties ||
            !fVulkanFunctions.vkGetPhysicalDeviceMemoryProperties ||
            !fVulkanFunctions.vkAllocateMemory ||
            !fVulkanFunctions.vkFreeMemory ||
            !fVulkanFunctions.vkMapMemory ||
            !fVulkanFunctions.vkUnmapMemory ||
            !fVulkanFunctions.vkFlushMappedMemoryRanges ||
            !fVulkanFunctions.vkInvalidateMappedMemoryRanges ||
            !fVulkanFunctions.vkBindBufferMemory ||
            !fVulkanFunctions.vkBindImageMemory ||
            !fVulkanFunctions.vkGetBufferMemoryRequirements ||
            !fVulkanFunctions.vkGetImageMemoryRequirements ||
            !fVulkanFunctions.vkCreateBuffer ||
            !fVulkanFunctions.vkDestroyBuffer ||
            !fVulkanFunctions.vkCreateImage ||
            !fVulkanFunctions.vkDestroyImage ||
            !fVulkanFunctions.vkCmdCopyBuffer) {
            fAllocator = VK_NULL_HANDLE;
            return;
        }

        VmaAllocatorCreateInfo createInfo = {};
        createInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT |
                           VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
        createInfo.instance = backendContext.fInstance;
        createInfo.physicalDevice = backendContext.fPhysicalDevice;
        createInfo.device = backendContext.fDevice;
        createInfo.preferredLargeHeapBlockSize = 4 * 1024 * 1024;
        createInfo.vulkanApiVersion = VK_API_VERSION_1_1;
        createInfo.pVulkanFunctions = &fVulkanFunctions;

        VkResult res = vmaCreateAllocator(&createInfo, &fAllocator);
        if (res != VK_SUCCESS) {
            fAllocator = VK_NULL_HANDLE;
        }
    }

    ~GpuOpenVulkanMemoryAllocator() override {
        if (fAllocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(fAllocator);
            fAllocator = VK_NULL_HANDLE;
        }
    }

    bool isValid() const { return fAllocator != VK_NULL_HANDLE; }

    VkResult allocateImageMemory(VkImage image,
                                 uint32_t allocationPropertyFlags,
                                 skgpu::VulkanBackendMemory* memory) override {
        if (!memory || fAllocator == VK_NULL_HANDLE) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
        allocCreateInfo.flags = 0;
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        allocCreateInfo.preferredFlags = 0;
        if (allocationPropertyFlags & kDedicatedAllocation_AllocationPropertyFlag) {
            allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }
        if (allocationPropertyFlags & kLazyAllocation_AllocationPropertyFlag) {
            allocCreateInfo.requiredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        }
        if (allocationPropertyFlags & kProtected_AllocationPropertyFlag) {
            allocCreateInfo.requiredFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
        }

        VmaAllocation allocation = VK_NULL_HANDLE;
        VkResult res = vmaAllocateMemoryForImage(fAllocator, image, &allocCreateInfo,
                                                 &allocation, nullptr);
        if (res == VK_SUCCESS) {
            *memory = static_cast<skgpu::VulkanBackendMemory>(reinterpret_cast<intptr_t>(allocation));
        }
        return res;
    }

    VkResult allocateBufferMemory(VkBuffer buffer,
                                  BufferUsage usage,
                                  uint32_t allocationPropertyFlags,
                                  skgpu::VulkanBackendMemory* memory) override {
        if (!memory || fAllocator == VK_NULL_HANDLE) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = 0;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
        switch (usage) {
            case BufferUsage::kGpuOnly:
                allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                allocCreateInfo.preferredFlags = 0;
                break;
            case BufferUsage::kCpuWritesGpuReads:
                allocCreateInfo.requiredFlags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case BufferUsage::kTransfersFromCpuToGpu:
                allocCreateInfo.requiredFlags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                allocCreateInfo.preferredFlags = 0;
                break;
            case BufferUsage::kTransfersFromGpuToCpu:
                allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                break;
        }

        if (allocationPropertyFlags & kDedicatedAllocation_AllocationPropertyFlag) {
            allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }
        if ((allocationPropertyFlags & kLazyAllocation_AllocationPropertyFlag) &&
            usage == BufferUsage::kGpuOnly) {
            allocCreateInfo.preferredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        }
        if (allocationPropertyFlags & kPersistentlyMapped_AllocationPropertyFlag) {
            allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }
        if (allocationPropertyFlags & kProtected_AllocationPropertyFlag) {
            allocCreateInfo.requiredFlags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
        }

        VmaAllocation allocation = VK_NULL_HANDLE;
        VkResult res = vmaAllocateMemoryForBuffer(fAllocator, buffer, &allocCreateInfo,
                                                  &allocation, nullptr);
        if (res == VK_SUCCESS) {
            *memory = static_cast<skgpu::VulkanBackendMemory>(reinterpret_cast<intptr_t>(allocation));
        }
        return res;
    }

    void getAllocInfo(const skgpu::VulkanBackendMemory& backendMemory,
                      skgpu::VulkanAlloc* alloc) const override {
        if (!alloc || backendMemory == 0 || fAllocator == VK_NULL_HANDLE) {
            return;
        }

        VmaAllocation allocation = reinterpret_cast<VmaAllocation>(backendMemory);
        VmaAllocationInfo allocInfo = {};
        vmaGetAllocationInfo(fAllocator, allocation, &allocInfo);

        alloc->fMemory = allocInfo.deviceMemory;
        alloc->fOffset = allocInfo.offset;
        alloc->fSize = allocInfo.size;
        alloc->fBackendMemory = backendMemory;
        alloc->fFlags = 0;

        VkMemoryPropertyFlags props = 0;
        vmaGetMemoryTypeProperties(fAllocator, allocInfo.memoryType, &props);
        if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            alloc->fFlags |= skgpu::VulkanAlloc::kMappable_Flag;
        }
        if ((props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            alloc->fFlags |= skgpu::VulkanAlloc::kNoncoherent_Flag;
        }
        if (props & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
            alloc->fFlags |= skgpu::VulkanAlloc::kLazilyAllocated_Flag;
        }
    }

    VkResult mapMemory(const skgpu::VulkanBackendMemory& backendMemory, void** data) override {
        if (!data || backendMemory == 0 || fAllocator == VK_NULL_HANDLE) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        VmaAllocation allocation = reinterpret_cast<VmaAllocation>(backendMemory);
        return vmaMapMemory(fAllocator, allocation, data);
    }

    void unmapMemory(const skgpu::VulkanBackendMemory& backendMemory) override {
        if (backendMemory == 0 || fAllocator == VK_NULL_HANDLE) {
            return;
        }
        VmaAllocation allocation = reinterpret_cast<VmaAllocation>(backendMemory);
        vmaUnmapMemory(fAllocator, allocation);
    }

    VkResult flushMemory(const skgpu::VulkanBackendMemory& backendMemory,
                         VkDeviceSize offset,
                         VkDeviceSize size) override {
        if (backendMemory == 0 || fAllocator == VK_NULL_HANDLE) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        VmaAllocation allocation = reinterpret_cast<VmaAllocation>(backendMemory);
        return vmaFlushAllocation(fAllocator, allocation, offset, size);
    }

    VkResult invalidateMemory(const skgpu::VulkanBackendMemory& backendMemory,
                              VkDeviceSize offset,
                              VkDeviceSize size) override {
        if (backendMemory == 0 || fAllocator == VK_NULL_HANDLE) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        VmaAllocation allocation = reinterpret_cast<VmaAllocation>(backendMemory);
        return vmaInvalidateAllocation(fAllocator, allocation, offset, size);
    }

    void freeMemory(const skgpu::VulkanBackendMemory& backendMemory) override {
        if (backendMemory == 0 || fAllocator == VK_NULL_HANDLE) {
            return;
        }
        VmaAllocation allocation = reinterpret_cast<VmaAllocation>(backendMemory);
        vmaFreeMemory(fAllocator, allocation);
    }

    std::pair<uint64_t, uint64_t> totalAllocatedAndUsedMemory() const override {
        if (fAllocator == VK_NULL_HANDLE) {
            return {0, 0};
        }
        VmaTotalStatistics stats = {};
        vmaCalculateStatistics(fAllocator, &stats);
        return {stats.total.statistics.blockBytes, stats.total.statistics.allocationBytes};
    }

private:
    VmaAllocator fAllocator = VK_NULL_HANDLE;
    VmaVulkanFunctions fVulkanFunctions = {};
};

}  // namespace

sk_sp<skgpu::VulkanMemoryAllocator> makeGpuOpenVulkanMemoryAllocator(
    const skgpu::VulkanBackendContext& backendContext) {
    sk_sp<GpuOpenVulkanMemoryAllocator> allocator =
        sk_make_sp<GpuOpenVulkanMemoryAllocator>(backendContext);
    return allocator->isValid() ? allocator : nullptr;
}
