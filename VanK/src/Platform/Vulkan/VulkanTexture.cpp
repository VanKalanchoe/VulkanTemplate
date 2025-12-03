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
            case ImageFormat::RGB8: return vk::Format::eB8G8R8Srgb;
            case ImageFormat::RGBA8: return vk::Format::eB8G8R8A8Srgb; //eR8G8B8A8Unorm
            default: return vk::Format::eUndefined;
        }
    }

    VulkanTexture2D::VulkanTexture2D(const TextureSpecification& specification, Buffer data) : m_Specification(specification), m_Width(m_Specification.Width), m_Height(m_Specification.Height)
    {
        RenderCommand::waitForGraphicsQueueIdle();
        
        auto& instance = VulkanRendererAPI::Get();
        
        /*if (instance.GetTextureCount() >= instance.GetMaxTexture())
        {
            instance.ResizeDescriptor(); //change this wont work i have multiple pipelines now
        }*/
        
        utils::ImageResource local{};
        
        // Determine the Vulkan format from KTX format
        vk::Format textureFormat;
        ktxTexture2* ktx_texture = m_Specification.ktTexture;
    
        uint32_t texWidth = m_Specification.Width;
        uint32_t texHeight = m_Specification.Height;
    
        if (!ktx_texture)
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
           
            // Create the texture image
            vk::ImageCreateInfo imageInfo
            {
                .imageType = vk::ImageType::e2D, .format = textureFormat,
                .extent = {texWidth, texHeight, 1}, .mipLevels = mipLevels, .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, .sharingMode = vk::SharingMode::eExclusive
            };
            
            local.image = instance.GetAllocator().createImage(imageInfo).image;
            DBG_VK_NAME(*local.image);
            
            auto commandBuffer = utils::beginSingleTimeCommands(instance.GetDevice(), instance.GetCommandPool());
            
            utils::transitionImageLayout(*commandBuffer, local.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
        
            VulkanRendererAPI::Get().GetAllocator().copyBufferToImage(commandBuffer, stagingBuffer, local.image, texWidth, texHeight);
        
            utils::transitionImageLayout(*commandBuffer, local.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
            
            utils::endSingleTimeCommands(*commandBuffer, instance.GetQueue());
            
            // Create the texture view maybe make info here like createimage ?
            local.view = VulkanRendererAPI::Get().createImageView(local.image, textureFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
            if (local.view == VK_NULL_HANDLE)
                std::cout << "VulkanTexture2D: Failed to create texture view!" << std::endl;
            DBG_VK_NAME(*local.view);
            
            local.extent.width = texWidth;
            local.extent.height = texHeight;
            local.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        
            m_TextureIndex = VulkanRendererAPI::Get().AddTextureToPool(std::move(local));
        
            LOGI("VulkanTexture2D: Created texture with index: %u", m_TextureIndex);

            VulkanRendererAPI::Get().GetAllocator().freeStagingBuffers();
        
            // Return early — do not run KTX logic
            return;
        }
        
        utils::Buffer stagingBuffer = VulkanRendererAPI::Get().GetAllocator().createStagingBuffer(std::span(data.Data, data.Size));
    
        // Check if the KTX texture has a format
        if (ktx_texture->classId == ktxTexture2_c)
        {
            textureFormat = static_cast<vk::Format>(specification.ktTexture->vkFormat);
            if (textureFormat == vk::Format::eUndefined)
            {
                // If the format is undefined, fall back to a reasonable default
                textureFormat = vk::Format::eB8G8R8A8Srgb; // srgb ?
            }
        }
        else
        {
            // For KTX1 files or if we can't determine the format, use a reasonable default
            textureFormat = vk::Format::eB8G8R8A8Srgb;
        }
    
        textureImageFormat = textureFormat;
    
        if (ktx_texture->numLevels > 1)
        {
            if (m_Specification.GenerateMips)
                mipLevels = ktx_texture->numLevels;
            else
                mipLevels = 1;
            
            // Create the texture image
            vk::ImageCreateInfo imageInfo
            {
                .imageType = vk::ImageType::e2D, .format = textureFormat,
                .extent = {texWidth, texHeight, 1}, .mipLevels = mipLevels, .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                .sharingMode = vk::SharingMode::eExclusive
            };
            
            local.image = instance.GetAllocator().createImage(imageInfo).image;
            DBG_VK_NAME(*local.image);
            
            // Copy data from staging buffer to texture image
            
            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = utils::beginSingleTimeCommands(instance.GetDevice(), instance.GetCommandPool());
            
            utils::transitionImageLayout(*commandBuffer, local.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
            
            // Copy each mip level
            for (uint32_t i = 0; i < mipLevels; i++)
            {
                ktx_size_t offset;
                KTX_error_code result = ktxTexture_GetImageOffset((ktxTexture*)ktx_texture, i, 0, 0, &offset);
                uint32_t mipWidth = std::max(1u, texWidth >> i);
                uint32_t mipHeight = std::max(1u, texHeight >> i);
            
                VulkanRendererAPI::Get().GetAllocator().copyBufferToImage(commandBuffer, stagingBuffer,local.image, mipWidth, mipHeight, offset, i);
            }
            
            utils::transitionImageLayout(*commandBuffer, local.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
            
            utils::endSingleTimeCommands(*commandBuffer, instance.GetQueue());
        }
        else
        {
            if (m_Specification.GenerateMips)
                mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
            else
                mipLevels = 1;
            
            // Create the texture image
            vk::ImageCreateInfo imageInfo
            {
                .imageType = vk::ImageType::e2D, .format = textureFormat,
                .extent = {texWidth, texHeight, 1}, .mipLevels = mipLevels, .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                .sharingMode = vk::SharingMode::eExclusive
            };
            
            local.image = instance.GetAllocator().createImage(imageInfo).image;
            DBG_VK_NAME(*local.image);
            
            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = utils::beginSingleTimeCommands(instance.GetDevice(), instance.GetCommandPool());
            
            // Copy data from staging buffer to texture image
            utils::transitionImageLayout(*commandBuffer, local.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
        
            VulkanRendererAPI::Get().GetAllocator().copyBufferToImage(commandBuffer, stagingBuffer, local.image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
            
            utils::endSingleTimeCommands(*commandBuffer, instance.GetQueue());
            
            if (mipLevels > 1 && m_Specification.GenerateMips)
                VulkanRendererAPI::Get().generateMipmaps(local.image, textureFormat, texWidth, texHeight, mipLevels);
        }
        
        // Create the texture view maybe make info here like createimage ?
        local.view = VulkanRendererAPI::Get().createImageView(local.image, textureFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
        if (local.view == VK_NULL_HANDLE)
            std::cout << "VulkanTexture2D: Failed to create texture view!" << '\n';
        
        DBG_VK_NAME(*local.view);
        
        local.extent.width = texWidth;
        local.extent.height = texHeight;
        local.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
       
        m_TextureIndex = VulkanRendererAPI::Get().AddTextureToPool(std::move(local));
        
        LOGI("VulkanTexture2D: Created texture with index: %u", m_TextureIndex);
    
        VulkanRendererAPI::Get().GetAllocator().freeStagingBuffers();
    
        // Cleanup KTX resources
        ktxTexture_Destroy((ktxTexture*)ktx_texture);
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
            *VulkanRendererAPI::Get().GetImageSource(m_TextureIndex).view, 
            static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)
        );

        s_ImGuiDescriptorSets[this] = ds;
        return (ImTextureID)ds;
    }
}