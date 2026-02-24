#pragma once

#include "VulkanRendererAPI.h"

#include "tracy/TracyVulkan.hpp"

#define VANKGPU_PROFILER_ZONE(cmd, name) \
TracyVkZoneTransient(VanK::VulkanProfilerAPI::g_TracyVkCtx, ___tracy_gpu_zone, *(Unwrap(cmd)), name, true)
/*#define VANKGPU_PROFILER_ZONE_COLOR(cmd, literal, color) \
TracyVkZoneC(VanK::VulkanProfilerAPI::g_TracyVkCtx, *(cmd), literal, color)*/

namespace VanK
{
    class VulkanProfilerAPI : public RendererAPI
    {
    public:
        static void initVKProfilerAPI(const vk::raii::PhysicalDevice& physdev, const vk::raii::Device& device, const vk::raii::Queue& queue, const vk::raii::CommandBuffer& cmd);
        static void shutdownVKProfilerAPI();
        static void ProfilerVkCollect(const vk::raii::CommandBuffer& cmd);
      
        inline static TracyVkCtx g_TracyVkCtx = nullptr;
    };
}