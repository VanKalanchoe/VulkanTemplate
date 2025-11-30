#include "VulkanTexture.h"

#include "VanK/Core/logger.h"
#include "VanK/Renderer/RenderCommand.h"
#include "VanK/Renderer/Texture.h"

namespace VanK
{
    static std::unordered_map<VulkanTexture2D*, VkDescriptorSet> s_ImGuiDescriptorSets;
    
    vk::Format ConvertImageFormat(ImageFormat format)
    {
        switch (format)
        {
            case ImageFormat::RGB8: return vk::Format::eR8G8B8Unorm;
            case ImageFormat::RGBA8: return vk::Format::eR8G8B8A8Unorm;
            default: return vk::Format::eUndefined;
        }
    }

    VulkanTexture2D::VulkanTexture2D(const TextureSpecification& specification, Buffer data) : m_Specification(specification), m_Width(m_Specification.Width), m_Height(m_Specification.Height)
    {
        RenderCommand::waitForGraphicsQueueIdle();
        
        /*if (instance.GetTextureCount() >= instance.GetMaxTexture())
        {
            instance.ResizeDescriptor(); //change this wont work i have multiple pipelines now
        }*/
        
        // Determine the Vulkan format from KTX format
        vk::Format textureFormat;
    
        ktxTexture* kTexture = m_Specification.ktTexture;
    
        uint32_t texWidth = m_Specification.Width;
        uint32_t texHeight = m_Specification.Height;
    
        if (!kTexture)
        {
            // WHITE / fallback texture or something in a buffer like font rendering
            VK_CORE_WARN("ktTexture is null, creating default white texture");
            
            // If the user requested RGB8, we must expand it to RGBA8
            if (m_Specification.Format == ImageFormat::RGB8)
            {
                uint32_t pixelCount = texWidth * texHeight;

                std::vector<uint8_t> converted(pixelCount * 4); // RGBA output
                uint8_t* src = (uint8_t*)data.Data;

                for (uint32_t i = 0; i < pixelCount; i++)
                {
                    converted[i*4 + 0] = src[i*3 + 0]; // R
                    converted[i*4 + 1] = src[i*3 + 1]; // G
                    converted[i*4 + 2] = src[i*3 + 2]; // B
                    converted[i*4 + 3] = 255;          // A = full opacity
                }
                
                // Allocate new Buffer and copy the data
                Buffer newData(converted.size());
                memcpy(newData.Data, converted.data(), converted.size());

                // Replace input buffer with RGBA buffer
                data = newData;  // now data owns the RGBA memory

                // Fix format
                m_Specification.Format = ImageFormat::RGBA8;
            }
            
            // Create staging buffer after conversion
            utils::Buffer stagingBuffer = VulkanRendererAPI::Get().GetAllocator().createStagingBuffer(std::span(data.Data, data.Size));
            
            textureFormat = ConvertImageFormat(m_Specification.Format);
            mipLevels = 1;
        
            VulkanRendererAPI::Get().createImage(
                texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, textureFormat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                textureImage, textureImageMemory
            );
        
            VulkanRendererAPI::Get().transitionImageLayout(textureImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
        
            auto commandBuffer = VulkanRendererAPI::Get().beginSingleTimeCommands();
        
            VulkanRendererAPI::Get().GetAllocator().copyBufferToImage(commandBuffer, stagingBuffer, textureImage, texWidth, texHeight);
        
            VulkanRendererAPI::Get().endSingleTimeCommands(*commandBuffer);
        
            VulkanRendererAPI::Get().transitionImageLayout(textureImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);

        
            textureImageView = VulkanRendererAPI::Get().createImageView(textureImage, textureFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
            DBG_VK_NAME(*textureImageView);
            
            utils::ImageResource resource{};
            resource.image = *textureImage;
            resource.view = *textureImageView;
            resource.extent.width = texWidth;
            resource.extent.height = texHeight;
            resource.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        
            m_TextureIndex = VulkanRendererAPI::Get().AddTextureToPool(std::move(resource));
        
            LOGI("VulkanTexture2D: Created texture with index: %u", m_TextureIndex);

            VulkanRendererAPI::Get().GetAllocator().freeStagingBuffers();
        
            // Return early — do not run KTX logic
            return;
        }
        
        utils::Buffer stagingBuffer = VulkanRendererAPI::Get().GetAllocator().createStagingBuffer(std::span(data.Data, data.Size));
    
        // Check if the KTX texture has a format
        if (kTexture->classId == ktxTexture2_c)
        {
            // For KTX2 files, we can get the format directly
            auto* ktx2 = reinterpret_cast<ktxTexture2*>(kTexture);
            if (ktxTexture2_NeedsTranscoding(ktx2))
            {
                std::cout << "This KTX2 is BASIS compressed and needs transcoding!" << std::endl;
                // Choose your GPU format
                textureFormat = vk::Format::eR8G8B8A8Unorm;

                KTX_error_code ec = ktxTexture2_TranscodeBasis(
                    ktx2,
                    KTX_TTF_RGBA32,      // matches eR8G8B8A8
                    0
                );
            }
            else
            {
                textureFormat = static_cast<vk::Format>(ktx2->vkFormat);
                if (textureFormat == vk::Format::eUndefined)
                {
                    // If the format is undefined, fall back to a reasonable default
                    textureFormat = vk::Format::eR8G8B8A8Unorm;
                }
            }
        }
        else
        {
            // For KTX1 files or if we can't determine the format, use a reasonable default
            textureFormat = vk::Format::eR8G8B8A8Unorm;
        }
    
        textureImageFormat = textureFormat;
    
        if (kTexture->numLevels > 1)
        {
            mipLevels = kTexture->numLevels;
            // Create the texture image
            VulkanRendererAPI::Get().createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, textureFormat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                        vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage,
                        textureImageMemory);

            DBG_VK_NAME(*textureImage);

            // Copy data from staging buffer to texture image
            VulkanRendererAPI::Get().transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);

            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = VulkanRendererAPI::Get().beginSingleTimeCommands();
            // Copy each mip level
            for (uint32_t i = 0; i < mipLevels; i++)
            {
                ktx_size_t offset;
                KTX_error_code result = ktxTexture_GetImageOffset(kTexture, i, 0, 0, &offset);
                uint32_t mipWidth = std::max(1u, texWidth >> i);
                uint32_t mipHeight = std::max(1u, texHeight >> i);
            
                VulkanRendererAPI::Get().GetAllocator().copyBufferToImage(commandBuffer, stagingBuffer,textureImage, static_cast<uint32_t>(mipWidth), static_cast<uint32_t>(mipHeight), offset, i);
            }
            VulkanRendererAPI::Get().endSingleTimeCommands(*commandBuffer);
    
            VulkanRendererAPI::Get().transitionImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
        }
        else
        {
            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = VulkanRendererAPI::Get().beginSingleTimeCommands();
            mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
            // Create the texture image
            VulkanRendererAPI::Get().createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, textureFormat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                        vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage,
                        textureImageMemory);

            DBG_VK_NAME(*textureImage);

            // Copy data from staging buffer to texture image
            VulkanRendererAPI::Get().transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
        
            VulkanRendererAPI::Get().GetAllocator().copyBufferToImage(commandBuffer, stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
            VulkanRendererAPI::Get().endSingleTimeCommands(*commandBuffer);
            VulkanRendererAPI::Get().generateMipmaps(textureImage, textureFormat, texWidth, texHeight, mipLevels);
        }

        textureImageView = VulkanRendererAPI::Get().createImageView(textureImage, textureImageFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
        DBG_VK_NAME(*textureImageView);
        
        utils::ImageResource resource{};
        resource.image = *textureImage;
        resource.view = *textureImageView;
        resource.extent.width = texWidth;
        resource.extent.height = texHeight;
        resource.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        
        m_TextureIndex = VulkanRendererAPI::Get().AddTextureToPool(std::move(resource));
        
        LOGI("VulkanTexture2D: Created texture with index: %u", m_TextureIndex);
    
        VulkanRendererAPI::Get().GetAllocator().freeStagingBuffers();
    
        // Cleanup KTX resources
        ktxTexture_Destroy(kTexture);
    }

    VulkanTexture2D::~VulkanTexture2D()
    {//fix what if instance already destoryed ??? and more
        // Remove from texture pool (optional, if you stored it there)
        if (m_TextureIndex != UINT32_MAX)
        {
            VulkanRendererAPI::Get().RemoveTextureFromPool(m_TextureIndex);
            m_TextureIndex = UINT32_MAX;
        }
        
        // Remove ImGui descriptor set if it exists
        if (s_ImGuiDescriptorSets.contains(this))
        {
            ImGui_ImplVulkan_RemoveTexture(s_ImGuiDescriptorSets[this]);
            s_ImGuiDescriptorSets.erase(this);
        }
    }

    ImTextureID VulkanTexture2D::getImTextureID()
    {
        // Return cached descriptor set if available
        if (s_ImGuiDescriptorSets.contains(this))
        {
            return (ImTextureID)s_ImGuiDescriptorSets[this];
        }

        // Create a new descriptor set for ImGui
        // We use the shared texture sampler from the RendererAPI
        vk::Sampler sampler = *VulkanRendererAPI::Get().textureSampler;
        
        VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
            sampler, 
            *textureImageView, 
            static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)
        );

        s_ImGuiDescriptorSets[this] = ds;
        return (ImTextureID)ds;
    }
}