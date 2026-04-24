#pragma once

#include "include/core/SkRefCnt.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"

sk_sp<skgpu::VulkanMemoryAllocator> makeGpuOpenVulkanMemoryAllocator(
    const skgpu::VulkanBackendContext& backendContext);
