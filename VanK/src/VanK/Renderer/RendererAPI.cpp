#include "RendererAPI.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"
// #include "MetalRendererAPI.h" // For future Metal support
namespace VanK
{
    RenderAPIType RendererAPI::s_API = RenderAPIType::Vulkan; // Default

    std::unique_ptr<RendererAPI> RendererAPI::Create(const Config& config)
    {
        switch (s_API)
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return std::make_unique<VulkanRendererAPI>(config);
            // case RenderAPIType::Metal: return std::make_unique<MetalRendererAPI>();

        default: return nullptr;
        }
    }
}