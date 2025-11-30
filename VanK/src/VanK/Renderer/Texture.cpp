#include "Texture.h"

#include "Platform/Vulkan/VulkanTexture.h"
#include "VanK/Renderer/RendererAPI.h"

namespace VanK
{
    size_t Texture2D::s_ImGuiTextureCount = 0;
    
    Ref<Texture2D> Texture2D::Create(const TextureSpecification& specification, Buffer data)
    {
        switch (RendererAPI::GetAPI())
        {
            case RenderAPIType::None: return nullptr;
            case RenderAPIType::Vulkan: return CreateRef<VulkanTexture2D>(specification, data);
            default: return nullptr;
        }
    }
}
