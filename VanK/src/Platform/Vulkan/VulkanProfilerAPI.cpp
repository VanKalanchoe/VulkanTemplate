#include "VulkanProfilerAPI.h"

namespace VanK
{
    void VulkanProfilerAPI::initVKProfilerAPI(const vk::raii::PhysicalDevice& physdev, const vk::raii::Device& device, const vk::raii::Queue& queue, const vk::raii::CommandBuffer& cmd)
    {
        g_TracyVkCtx = TracyVkContext(*physdev, *device, *queue, *cmd);
    }
    
    void VulkanProfilerAPI::shutdownVKProfilerAPI()
    {
        TracyVkDestroy(g_TracyVkCtx);
        g_TracyVkCtx = nullptr;
    }
    
    void VulkanProfilerAPI::ProfilerVkCollect(const vk::raii::CommandBuffer& cmd)
    {
        if (!g_TracyVkCtx) 
            return;
        
        TracyVkCollect(g_TracyVkCtx, *cmd);
    }
}
