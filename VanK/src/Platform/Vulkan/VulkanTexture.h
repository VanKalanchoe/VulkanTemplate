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
        
        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }
        virtual uint32_t GetTextureIndex() const override { return m_TextureIndex; }
        virtual ImTextureID getImTextureID() override;
        
    private:
        TextureSpecification m_Specification;
        
        uint32_t m_Width, m_Height;
        uint32_t m_TextureIndex = 0;
        
        vk::Format textureImageFormat = vk::Format::eUndefined;
        uint32_t mipLevels = 0;
        
        vk::raii::Image textureImage = nullptr;
        vk::raii::DeviceMemory textureImageMemory = nullptr;
        vk::raii::ImageView textureImageView = nullptr;
    };
}
