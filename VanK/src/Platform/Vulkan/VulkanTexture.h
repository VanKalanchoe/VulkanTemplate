#pragma once

#include "VulkanRendererAPI.h"

#include "VanK/Renderer/Texture.h"

namespace VanK
{
    class VulkanTexture2D : public Texture2D
    {
    public:
        VulkanTexture2D(const TextureSpecification& specification, Buffer data = Buffer());
        virtual ~VulkanTexture2D() override;
        
        virtual const TextureSpecification& GetSpecification() const override { return m_Specification; }
        
        virtual uint32_t GetWidth() const override { return m_Specification.Width; }
        virtual uint32_t GetHeight() const override { return m_Specification.Height; }
        virtual uint32_t GetTextureIndex() const override { return m_TextureIndex; }
        virtual ImTextureID getImTextureID() override;
        
    private:
        TextureSpecification m_Specification;
        VkDescriptorSet m_ImGuiHandle = nullptr;
        uint32_t m_TextureIndex = 0;
        vk::Format textureImageFormat = vk::Format::eUndefined; // do i need this here ? 
        uint32_t mipLevels = 0; // do i need this here ? 
    };
}
