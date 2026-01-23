#include "VulkanTexture.h"

#include "VanK/Core/logger.h"
#include "VanK/Renderer/RenderCommand.h"
#include "VanK/Renderer/Texture.h"

namespace VanK
{
    vk::Filter ConvertToVkFilter(VanKFilter vankFilter)
    {
        switch (vankFilter)
        {
            case VanKFilter::filterNearest: return vk::Filter::eNearest;
            case VanKFilter::filterLinear: return vk::Filter::eLinear;
            default:
            {
                std::cout << "Invalid Filter" << '\n';
                return vk::Filter::eLinear;
            }
        }
    }
    
    vk::SamplerMipmapMode ConvertToVkSamplerMipmapMode(VanKSamplerMipmapMode vankMode)
    {
        switch (vankMode)
        {
            case VanKSamplerMipmapMode::mipmapModeNearest : return vk::SamplerMipmapMode::eNearest;
            case VanKSamplerMipmapMode::mipmapModeLinear : return vk::SamplerMipmapMode::eLinear;
            default:
            {
                std::cout << "Invalid Sampler Mipmap Mode" << '\n';
                return vk::SamplerMipmapMode::eLinear;
            }
        }
    }
    
    vk::SamplerAddressMode ConvertToVkSamplerAddressMode(VanKSamplerAddressMode vankMode)
    {
        switch (vankMode)
        {
            case VanKSamplerAddressMode::addressModeRepeat : return vk::SamplerAddressMode::eRepeat;
            case VanKSamplerAddressMode::addressModeMirrorRepeat : return vk::SamplerAddressMode::eMirroredRepeat;
            case VanKSamplerAddressMode::addressModeClampToEdge : return vk::SamplerAddressMode::eClampToEdge;
            case VanKSamplerAddressMode::addressModeClampToBorder : return vk::SamplerAddressMode::eClampToBorder;
            case VanKSamplerAddressMode::addressModeMirrorClampToEdge : return vk::SamplerAddressMode::eMirrorClampToEdge;
            default:
            {
                std::cout << "Invalid Sampler Address Mode" << '\n';
                return vk::SamplerAddressMode::eRepeat;
            }
        }
    }
    
    vk::CompareOp ConvertToVkCompareOp(VanKCompareOp vankCompareOp)
    {
        switch (vankCompareOp)
        {
            case VanKCompareOp::compareOpNever : return vk::CompareOp::eNever;
            case VanKCompareOp::compareOpLess : return vk::CompareOp::eLess;
            case VanKCompareOp::compareOpEqual : return vk::CompareOp::eEqual;
            case VanKCompareOp::compareOpLessOrEqual : return vk::CompareOp::eLessOrEqual;
            case VanKCompareOp::compareOpGreater : return vk::CompareOp::eGreater;
            case VanKCompareOp::compareOpNotEqual : return vk::CompareOp::eNotEqual;
            case VanKCompareOp::compareOpGreaterOrEqual : return vk::CompareOp::eGreaterOrEqual;
            case VanKCompareOp::compareOpAlways : return vk::CompareOp::eAlways;
            default:
            {
                std::cout << "Invalid Sampler Compare Op" << '\n';
                return vk::CompareOp::eNever;
            }
        }
    }

    /*vk::Format ConvertImageFormat(ImageFormat format)
    {
        switch (format)
        {
            case ImageFormat::RGB8: return vk::Format::eB8G8R8Unorm;
            case ImageFormat::RGBA8: return vk::Format::eB8G8R8A8Unorm; //eR8G8B8A8Unorm
            case ImageFormat::SRGBA8: return vk::Format::eB8G8R8A8Srgb;
            case ImageFormat::R16G16: return vk::Format::eR16G16Sfloat;
            default: return vk::Format::eUndefined;
        }
    }*/
    vk::Format ConvertImageFormat(ImageFormat format)
    {
        switch (format)
        {
        case ImageFormat::RGB8: return vk::Format::eR8G8B8A8Unorm;
        case ImageFormat::RGBA8: return vk::Format::eR8G8B8A8Unorm; //eR8G8B8A8Unorm
        case ImageFormat::SRGBA8: return vk::Format::eR8G8B8A8Srgb;
        case ImageFormat::R16G16: return vk::Format::eR16G16Sfloat;
        default: return vk::Format::eUndefined;
        }
    }

    VulkanTexture2D::VulkanTexture2D(const TextureSpecification& specification, Buffer data) : m_Specification(specification)
    {
        RenderCommand::waitForGraphicsQueueIdle();
        
        auto& instance = VulkanRendererAPI::Get();
        
        /*if (instance.GetTextureCount() >= instance.GetMaxTexture())
        {
            instance.ResizeDescriptor(); //change this wont work i have multiple pipelines now
        }*/
        
        utils::ImageResource local{};
        
        // sampler
        VanKSamplerInfo info = m_Specification.SamplerInfo;
        vk::PhysicalDeviceProperties properties = instance.GetPhysicalDevice().getProperties();
        vk::SamplerCreateInfo samplerInfo
        {
            .magFilter = ConvertToVkFilter(info.magFilter),
            .minFilter = ConvertToVkFilter(info.minFilter),
            .mipmapMode = ConvertToVkSamplerMipmapMode(info.mipmapMode),
            .addressModeU = ConvertToVkSamplerAddressMode(info.addressModeU),
            .addressModeV = ConvertToVkSamplerAddressMode(info.addressModeV),
            .addressModeW = ConvertToVkSamplerAddressMode(info.addressModeW),
            .mipLodBias = info.mipLodBias, // only works if it has enoug miplevels is miplevel is max 1 then making this 10 crashes
            .anisotropyEnable = info.anisotopyEnable,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = info.compareEnable,
            .compareOp = ConvertToVkCompareOp(info.compareOp),
            .minLod = info.minLod, // the higher this is the lower the resolution 0 is max res
            .maxLod = VK_LOD_CLAMP_NONE
        };
        auto textureSampler = instance.getSamplerPool().acquireSampler(samplerInfo);
        local.sampler = textureSampler;
        
        /* not needed right now only with eclamptoborder im using edge
         *hashCombine(info.borderColor);
        hashCombine(info.unnormalizedCoordinates);
        */
        
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
                .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                .sharingMode = vk::SharingMode::eExclusive
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
                textureFormat = vk::Format::eR8G8B8A8Srgb; // srgb ?
            }
            
            if (textureFormat == vk::Format::eR32G32B32Sfloat)
                textureFormat = vk::Format::eR32G32B32A32Sfloat;
        }
        else
        {
            // For KTX1 files or if we can't determine the format, use a reasonable default
            textureFormat = vk::Format::eR8G8B8A8Srgb;
        }
    
        textureImageFormat = textureFormat;
        
        bool isCubeMap = ktx_texture->isCubemap;
        uint32_t layerCount = isCubeMap ? 6u : 1u;
        
        if (ktx_texture->numLevels > 1)
        {
            if (m_Specification.GenerateMips)
                mipLevels = ktx_texture->numLevels;
            else
                mipLevels = 1;
            
            // Create the texture image
            vk::ImageCreateInfo imageInfo
            {
                .flags = isCubeMap ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags(),
                .imageType = vk::ImageType::e2D, .format = textureFormat,
                .extent = {texWidth, texHeight, 1}, .mipLevels = mipLevels, .arrayLayers = layerCount,
                .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                .sharingMode = vk::SharingMode::eExclusive
            };
            
            local.image = instance.GetAllocator().createImage(imageInfo).image;
            DBG_VK_NAME(*local.image);
            
            // Copy data from staging buffer to texture image
            
            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = utils::beginSingleTimeCommands(instance.GetDevice(), instance.GetCommandPool());
            
            utils::transitionImageLayout(*commandBuffer, local.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels, layerCount);
            
            // Copy each mip level
            for (uint32_t i = 0; i < mipLevels; i++)
            {
                uint32_t mipWidth = std::max(1u, texWidth >> i);
                uint32_t mipHeight = std::max(1u, texHeight >> i);
            
                for (uint32_t face = 0; face < layerCount; face++)
                {
                    ktx_size_t offset;
                    KTX_error_code result = ktxTexture_GetImageOffset((ktxTexture*)ktx_texture, i, 0, face, &offset);
                    VulkanRendererAPI::Get().GetAllocator().copyBufferToImage(commandBuffer, stagingBuffer,local.image, mipWidth, mipHeight, offset, i, face);
                }
            }
            
            utils::transitionImageLayout(*commandBuffer, local.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels, layerCount);
            
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
                .flags = isCubeMap ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags(),
                .imageType = vk::ImageType::e2D, .format = textureFormat,
                .extent = {texWidth, texHeight, 1}, .mipLevels = mipLevels, .arrayLayers = layerCount,
                .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                .sharingMode = vk::SharingMode::eExclusive
            };
            
            local.image = instance.GetAllocator().createImage(imageInfo).image;
            DBG_VK_NAME(*local.image);
            
            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = utils::beginSingleTimeCommands(instance.GetDevice(), instance.GetCommandPool());
            
            // Copy data from staging buffer to texture image
            utils::transitionImageLayout(*commandBuffer, local.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels, layerCount);
        
            VulkanRendererAPI::Get().GetAllocator().copyBufferToImage(commandBuffer, stagingBuffer, local.image, texWidth, texHeight, 0, 0, 0);
            
            utils::endSingleTimeCommands(*commandBuffer, instance.GetQueue());
            
            if (mipLevels > 1 && m_Specification.GenerateMips)
                VulkanRendererAPI::Get().generateMipmaps(local.image, textureFormat, texWidth, texHeight, mipLevels, layerCount);
        }
        
        // Create the texture view maybe make info here like createimage ?
        vk::ImageViewType viewType = isCubeMap ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
        local.view = VulkanRendererAPI::Get().createImageView(local.image, textureFormat, vk::ImageAspectFlagBits::eColor, mipLevels, layerCount, viewType);
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
    {
        std::cout << "Destroying: " << m_Specification.Name << '\n';
        
        auto& instance = VulkanRendererAPI::Get();
        
        // Check 2: Is the renderer properly initialized?
        if (VulkanRendererAPI::IsInitialized())
            return; // Renderer not initialized
        
        /*// Check 3: Is our texture index valid?
        if (!instance.IsTextureValid(m_TextureIndex))
        {
            return; // Texture already removed or invalid index
        }*/

        /*// Check 4: Is the texture vector not empty?
        if (instance.GetTextureCount() == 0)
        {
            return; // No textures to remove
        }*/
        
        instance.waitForGraphicsQueueIdle();
            
        // Only remove handle if ImGui Vulkan is still alive
        if (m_ImGuiHandle && instance.isImGuiInit())
        {
            ImGui_ImplVulkan_RemoveTexture(m_ImGuiHandle);
            m_ImGuiHandle = nullptr;
        }
            
        SetNumImGuiTextures(GetNumImGuiTextures() - 1);
            
        if (m_TextureIndex != UINT32_MAX)
        {
            // All checks passed - safe to remove
            instance.RemoveTextureFromPool(m_TextureIndex);
            m_TextureIndex = UINT32_MAX;
        }
    }

    ImTextureID VulkanTexture2D::getImTextureID()
    {
        auto& instance = VulkanRendererAPI::Get();
        
        // If ImGui Vulkan backend is NOT available, return stored handle or nullptr
        if (!instance.isImGuiInit())
            return reinterpret_cast<ImTextureID>(m_ImGuiHandle);

        // If a handle already exists, return it
        if (m_ImGuiHandle)
            return reinterpret_cast<ImTextureID>(m_ImGuiHandle);

        auto& image = instance.GetImageSource(m_TextureIndex);
        
        // Validate that the renderer Vulkan objects are still alive
        if (!image.sampler)
            return 0;

        auto view = *image.view;
        if (!view)
            return 0;

        // Create descriptor for ImGui
        m_ImGuiHandle = ImGui_ImplVulkan_AddTexture(
            image.sampler,
            view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        
        if (m_ImGuiHandle)
        {
            SetNumImGuiTextures(GetNumImGuiTextures() + 1);
        }

        return reinterpret_cast<ImTextureID>(m_ImGuiHandle);
    }
}