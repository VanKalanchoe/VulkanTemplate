#pragma once

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <limits>
#include <array>
#include <assert.h>
#include <chrono>
#include <filesystem>
#include <span>
#include <thread>
#include <unordered_map>

#include "VanK/Renderer/RendererAPI.h"
#include "VanK/Core/Log.h"

#ifdef __INTELLISENSE__
#define VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#include <vulkan/vulkan_raii.hpp>
#else
#define VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
import vulkan_hpp;
#endif

// Disable warnings in VMA
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#pragma warning(push)
#pragma warning(disable : 4100)  // Unreferenced formal parameter
#pragma warning(disable : 4189)  // Local variable is initialized but not referenced
#pragma warning(disable : 4127)  // Conditional expression is constant
#pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#pragma warning(disable : 4505)  // Unreferenced function with internal linkage has been removed
#include "vk_mem_alloc.h"
#pragma warning(pop)
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_CXX11
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <ktx.h>

#include "Platform/Vulkan/debug_util.h"
/*#define DBG_VK_NAME(obj)  ((void)0)
#define DBG_VK_SCOPE(_cmd)  ((void)0)*/
// Some graphic user interface (GUI) using Dear ImGui
#include "backends/imgui_impl_SDL3.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui_internal.h"  // For Docking

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

const std::string TEXTURE_PATH = "../build/VanK/textures/viking_room.ktx2";
// Define the number of objects to render
constexpr int MAX_OBJECTS = 3;

const std::vector validationLayers =
{
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

/*-- 
 * Combines hash values using the FNV-1a based algorithm 
-*/
static std::size_t hashCombine(std::size_t seed, auto const& value)
{
    return seed ^ (std::hash<std::decay_t<decltype(value)>>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

// Macro to either assert or throw based on the build type
#ifdef NDEBUG
#define ASSERT(condition, message)                                                                                     \
do                                                                                                                   \
{                                                                                                                    \
if(!(condition))                                                                                                   \
{                                                                                                                  \
throw std::runtime_error(message);                                                                               \
}                                                                                                                  \
} while(false)
#else
#define ASSERT(condition, message) assert((condition) && (message))
#endif

namespace VanK
{
    struct VanKCommandBuffer_T
    {
        vk::raii::CommandBuffer* handle;
    };

    inline vk::raii::CommandBuffer& Unwrap(VanKCommandBuffer cmd)
    {
        assert(cmd && "VanKCommandBuffer is null!");
        return *cmd->handle;
    }

    struct VanKPipeLine_T
    {
        vk::Pipeline handle;
    };

    inline vk::Pipeline Unwrap(VanKPipeLine pipeline)
    {
        assert(pipeline && "VanKPipeLine is null!");
        return pipeline->handle;
    }

    inline VanKPipeLine Wrap(vk::Pipeline pipeline)
    {
        if (!pipeline)
            return nullptr;
       
        return new VanKPipeLine_T(pipeline);
    }
    
//--- Vulkan Helpers ------------------------------------------------------------------------------------------------------------
#ifdef NDEBUG
    #define VK_CHECK(vkFnc) vkFnc
#else
#define VK_CHECK(vkFnc) \
{ \
    VkResult rawResult = (vkFnc); \
    vk::Result checkResult = static_cast<vk::Result>(rawResult); \
    if(checkResult != vk::Result::eSuccess) \
    { \
        std::string errMsg = vk::to_string(checkResult); \
        /*LOGE("Vulkan error: %s", errMsg);    */ \
        ASSERT(checkResult == vk::Result::eSuccess, errMsg.c_str()); \
    } \
}
#endif
    
    namespace utils
    {
        [[nodiscard]] inline vk::raii::ShaderModule createShaderModule(vk::raii::Device& device,
                                                                       const std::span<const uint32_t>& code)
        {
            vk::ShaderModuleCreateInfo createInfo{
                .codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()
            };
            vk::raii::ShaderModule shaderModule{device, createInfo};
            /*assert(!*shaderModule && "Shader modules not compiled!");*/
            return shaderModule;
        }

        /*-- 
         * This returns the pipeline and access flags for a given layout, use for changing the image layout  
        -*/
        static std::tuple<vk::PipelineStageFlags2, vk::AccessFlags2> makePipelineStageAccessTuple(vk::ImageLayout state)
        {
            switch (state)
            {
            case vk::ImageLayout::eUndefined:
                return std::make_tuple(vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlagBits2::eNone);
            case vk::ImageLayout::eColorAttachmentOptimal:
                return std::make_tuple(vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                       vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite);
            case vk::ImageLayout::eShaderReadOnlyOptimal:
                return std::make_tuple(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader
                                       | vk::PipelineStageFlagBits2::ePreRasterizationShaders,
                                       vk::AccessFlagBits2::eShaderRead);
            case vk::ImageLayout::eTransferDstOptimal:
                return std::make_tuple(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            case vk::ImageLayout::eTransferSrcOptimal:
                return std::make_tuple(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
            case vk::ImageLayout::eGeneral:
                return std::make_tuple(vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTransfer,
                                       vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite |
                                       vk::AccessFlagBits2::eTransferWrite);
            case vk::ImageLayout::ePresentSrcKHR:
                return std::make_tuple(vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eNone);
            case vk::ImageLayout::eDepthStencilAttachmentOptimal: // added this myself
                return std::make_tuple(
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                );
            default:
                {
                    ASSERT(false, "Unsupported layout transition!");
                    return std::make_tuple(vk::PipelineStageFlagBits2::eAllCommands,
                                           vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
                }
            }
        };

        /*-- 
         * Return the barrier with the most common pair of stage and access flags for a given layout 
        -*/
        static vk::ImageMemoryBarrier2 createImageMemoryBarrier
        (
            vk::Image image,
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout,
            vk::ImageSubresourceRange subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        )
        {
            const auto [srcStage, srcAccess] = makePipelineStageAccessTuple(oldLayout);
            const auto [dstStage, dstAccess] = makePipelineStageAccessTuple(newLayout);

            vk::ImageMemoryBarrier2 barrier
            {
                .srcStageMask = srcStage,
                .srcAccessMask = srcAccess,
                .dstStageMask = dstStage,
                .dstAccessMask = dstAccess,
                .oldLayout = oldLayout,
                .newLayout = newLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = subresourceRange
            };
            return barrier;
        }

        /*--
         * A helper function to transition an image from one layout to another.
         * In the pipeline, the image must be in the correct layout to be used, and this function is used to transition the image to the correct layout.
        -*/
        static void cmdTransitionImageLayout
        (
            vk::raii::CommandBuffer& cmd,
            vk::Image image,
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout,
            vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
            uint32_t baseArrayLayer = 0,
            uint32_t layercount = 1
        )
        {
            const vk::ImageMemoryBarrier2 barrier = createImageMemoryBarrier(image, oldLayout, newLayout,
                                                                           {aspectMask, 0, 1, baseArrayLayer, layercount});
            const vk::DependencyInfo depInfo
            {
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier
            };
            cmd.pipelineBarrier2(depInfo);
        }

        /*-- 
        *  This helper returns the access mask for a given stage mask.
       -*/
        static vk::AccessFlags2 inferAccessMaskFromStage(vk::PipelineStageFlags2 stage, bool src)
        {
            vk::AccessFlags2 access = {};

            // Handle each possible stage bit
            if ((stage & vk::PipelineStageFlagBits2::eComputeShader))
                access |= src ? vk::AccessFlagBits2::eShaderRead : vk::AccessFlagBits2::eShaderWrite;
            if ((stage & vk::PipelineStageFlagBits2::eFragmentShader))
                access |= src ? vk::AccessFlagBits2::eShaderRead : vk::AccessFlagBits2::eShaderWrite;
            if ((stage & vk::PipelineStageFlagBits2::eVertexAttributeInput))
                access |= vk::AccessFlagBits2::eVertexAttributeRead; // Always read-only
            if ((stage & vk::PipelineStageFlagBits2::eTransfer))
                access |= src ? vk::AccessFlagBits2::eTransferRead : vk::AccessFlagBits2::eTransferWrite;
            ASSERT(access, "Missing stage implementation");
            return access;
        }

        /*--
         * This useful function simplifies the addition of buffer barriers, by inferring 
         * the access masks from the stage masks, and adding the buffer barrier to the command buffer.
        -*/
        static void cmdBufferMemoryBarrier
        (
            vk::raii::CommandBuffer& commandBuffer,
            vk::Buffer buffer,
            vk::PipelineStageFlags2 srcStageMask,
            vk::PipelineStageFlags2 dstStageMask,
            vk::AccessFlags2 srcAccessMask = {}, // Default to infer if not provided
            vk::AccessFlags2 dstAccessMask = {}, // Default to infer if not provided
            vk::DeviceSize offset = 0,
            vk::DeviceSize size = VK_WHOLE_SIZE,
            uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
        )
        {
            // Infer access masks if not explicitly provided
            if (!srcAccessMask)
            {
                srcAccessMask = inferAccessMaskFromStage(srcStageMask, true);
            }
            if (!dstAccessMask)
            {
                dstAccessMask = inferAccessMaskFromStage(dstStageMask, false);
            }

            const std::array<vk::BufferMemoryBarrier2, 1> bufferBarrier{
                {
                    {
                        .srcStageMask = srcStageMask,
                        .srcAccessMask = srcAccessMask,
                        .dstStageMask = dstStageMask,
                        .dstAccessMask = dstAccessMask,
                        .srcQueueFamilyIndex = srcQueueFamilyIndex,
                        .dstQueueFamilyIndex = dstQueueFamilyIndex,
                        .buffer = buffer,
                        .offset = offset,
                        .size = size
                    }
                }
            };

            const vk::DependencyInfo depInfo
            {
                .bufferMemoryBarrierCount = uint32_t(bufferBarrier.size()),
                .pBufferMemoryBarriers = bufferBarrier.data()
            };
            commandBuffer.pipelineBarrier2(depInfo);
        }
        
        /*--
         * A buffer is a region of memory used to store data.
         * It is used to store vertex data, index data, uniform data, and other types of data.
         * There is a VkBuffer object that represents the buffer, and a VmaAllocation object that represents the memory allocation.
         * The address is used to access the buffer in the shader.
        -*/
        struct Buffer
        {
            vk::Buffer buffer{}; // Vulkan Buffer
            VmaAllocation allocation{}; // Memory associated with the buffer
            vk::DeviceAddress address{}; // Address of the buffer in the shader
            vk::DeviceSize size{}; // Size of the buffer
        };

        /*--
         * An image is a region of memory used to store image data.
         * It is used to store texture data, framebuffer data, and other types of data.
        -*/
        struct Image
        {
            VkImage image{}; // Vulkan Image
            VmaAllocation allocation{}; // Memory associated with the image
        };

        /*-- 
         * The image resource is an image with an image view and a layout.
         * and other information like format and extent.
        -*/
        struct ImageResource : Image
        {
            VkImageView view{}; // Image view
            vk::Extent2D extent{}; // Size of the image
            vk::ImageLayout layout{}; // Layout of the image (color attachment, shader read, ...)
        };

        /*- Not implemented here -*/
        struct AccelerationStructure
        {
            vk::AccelerationStructureKHR accel{};
            VmaAllocation allocation{};
            vk::DeviceAddress deviceAddress{};
            vk::DeviceSize size{};
            Buffer buffer; // Underlying buffer
        };

        //--- Resource Allocator ------------------------------------------------------------------------------------------------------------
        /*--
         * Vulkan Memory Allocator (VMA) is a library that helps to manage memory in Vulkan.
         * This should be used to manage the memory of the resources instead of using the Vulkan API directly.
        -*/
        class ResourceAllocator
        {
        public:
            ResourceAllocator() = default;
            ~ResourceAllocator() { assert(m_allocator == nullptr && "Missing deinit()"); }
            operator VmaAllocator() const { return m_allocator; }

            // Initialization of VMA allocator.
            void init(VmaAllocatorCreateInfo allocatorInfo)
            {
                // #TODO : VK_EXT_memory_priority ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT

                allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
                // allow querying for the GPU address of a buffer
                allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
                allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
                // allow using VkBufferUsageFlags2CreateInfoKHR

                m_device = allocatorInfo.device;
                // Because we use VMA_DYNAMIC_VULKAN_FUNCTIONS
                const VmaVulkanFunctions functions = {
                    .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
                    .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
                };
                allocatorInfo.pVulkanFunctions = &functions;
                vmaCreateAllocator(&allocatorInfo, &m_allocator);
            }

            // De-initialization of VMA allocator.
            void deinit()
            {
                vmaDestroyAllocator(m_allocator);
                *this = {};
            }

            /*-- Create a buffer -*/
            /* 
             * UBO: VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
             *        + VMA_MEMORY_USAGE_CPU_TO_GPU
             * SSBO: VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
             *        + VMA_MEMORY_USAGE_CPU_TO_GPU // Use this if the CPU will frequently update the buffer
             *        + VMA_MEMORY_USAGE_GPU_ONLY // Use this if the CPU will rarely update the buffer
             *        + VMA_MEMORY_USAGE_GPU_TO_CPU  // Use this when you need to read back data from the SSBO to the CPU
             *      ----
             *        + VMA_ALLOCATION_CREATE_MAPPED_BIT // Automatically maps the buffer upon creation
             *        + VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT // If the CPU will sequentially write to the buffer's memory,
             */

            Buffer createBuffer(vk::DeviceSize size,
                                vk::BufferUsageFlags2KHR usage,
                                VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO,
                                VmaAllocationCreateFlags flags = {}) const
            {
                // This can be used only with maintenance5
                const vk::BufferUsageFlags2CreateInfoKHR bufferUsageFlags2CreateInfo{
                    .usage = usage | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                };

                const vk::BufferCreateInfo bufferInfo{
                    .pNext = &bufferUsageFlags2CreateInfo,
                    .size = size,
                    .usage = {},
                    .sharingMode = vk::SharingMode::eExclusive, // Only one queue family will access i
                };

                VmaAllocationCreateInfo allocInfo = {.flags = flags, .usage = memoryUsage};
                const vk::DeviceSize dedicatedMemoryMinSize = 64ULL * 1024; // 64 KB
                if (size > dedicatedMemoryMinSize)
                {
                    allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
                    // Use dedicated memory for large buffers
                }

                // Create the buffer
                Buffer resultBuffer;
                VmaAllocationInfo allocInfoOut{};
                VkBufferCreateInfo cbufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
                VkBuffer cbuffer = static_cast<VkBuffer>(resultBuffer.buffer);
                VK_CHECK(
                    vmaCreateBuffer(m_allocator, &cbufferInfo, &allocInfo, &cbuffer, &resultBuffer.allocation
                        , &
                        allocInfoOut));

                resultBuffer.buffer = cbuffer;
        
                // Get the GPU address of the buffer
                const vk::BufferDeviceAddressInfo info =
                {
                    .buffer = cbuffer
                };
                /*vk::DispatchLoaderDynamic dld(instance, m_device);
                resultBuffer.address = m_device.getBufferAddress(info);*/
                /*resultBuffer.address = vkGetBufferDeviceAddress(m_device, static_cast<VkBufferDeviceAddressInfo>(&info));*/
                {
                    // Find leaks
                    static uint32_t counter = 0U;
                    if (m_leakID == counter)
                    {
#if defined(_MSVC_LANG)
                        __debugbreak();
#endif
                    }
                    std::string allocID = std::string("allocID: ") + std::to_string(counter++);
                    vmaSetAllocationName(m_allocator, resultBuffer.allocation, allocID.c_str());
                }

                resultBuffer.size = size;
        
                return resultBuffer;
            }

            //*-- Destroy a buffer -*/
            void destroyBuffer(Buffer buffer) const { vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation); }

            void copyBuffer(std::unique_ptr<vk::raii::CommandBuffer>& commandBuffer, Buffer& srcBuffer, Buffer& dstBuffer, vk::DeviceSize size)
            {
                commandBuffer->copyBuffer(srcBuffer.buffer, dstBuffer.buffer, vk::BufferCopy{.size = size});
            }

            void copyBufferToImage(std::unique_ptr<vk::raii::CommandBuffer>& commandBuffer, const Buffer& buffer, vk::raii::Image& image, uint32_t width,
                                         uint32_t height,
                                         uint64_t offset = 0, uint32_t mipLevel = 0)
            {
                vk::BufferImageCopy region
                {
                    .bufferOffset = offset, .bufferRowLength = 0, .bufferImageHeight = 0,
                    .imageSubresource = {vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1},
                    .imageOffset = {0, 0, 0},
                    .imageExtent = {width, height, 1}
                };
                commandBuffer->copyBufferToImage(buffer.buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
            }

            /*--
             * Create a staging buffer, copy data into it, and track it.
             * This method accepts data, handles the mapping, copying, and unmapping
             * automatically.
            -*/
            template <typename T>
            Buffer createStagingBuffer(const std::span<T>& vectorData)
            {
                const VkDeviceSize bufferSize = sizeof(T) * vectorData.size();

                // Create a staging buffer
                Buffer stagingBuffer = createBuffer(bufferSize, vk::BufferUsageFlagBits2::eTransferSrc,
                                                    VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

                // Track the staging buffer for later cleanup
                m_stagingBuffers.push_back(stagingBuffer);

                // Map and copy data to the staging buffer
                void* data;
                vmaMapMemory(m_allocator, stagingBuffer.allocation, &data);
                memcpy(data, vectorData.data(), (size_t)bufferSize);
                vmaUnmapMemory(m_allocator, stagingBuffer.allocation);
                return stagingBuffer;
            }

            /*--
             * Create an image in GPU memory. This does not adding data to the image.
             * This is only creating the image in GPU memory.
             * See createImageAndUploadData for creating an image and uploading data.
            -*/
            Image createImage(const VkImageCreateInfo& imageInfo) const
            {
                const VmaAllocationCreateInfo createInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};

                Image image;
                VmaAllocationInfo allocInfo{};
                VK_CHECK(
                    vmaCreateImage(m_allocator, &imageInfo, &createInfo, &image.image, &image.allocation, &allocInfo));
                return image;
            }
            
            /*-- Destroy image --*/ // has to change once vma hpp
            void destroyImage(Image& image) const { vmaDestroyImage(m_allocator, image.image, image.allocation); }

            void destroyImageResource(ImageResource& imageRessource) const
            {
                destroyImage(imageRessource);
                vkDestroyImageView(m_device, imageRessource.view, nullptr);
            }

            /*-- Create an image and upload data using a staging buffer --*/
            template <typename T>
            ImageResource createImageAndUploadData
            (
                vk::raii::CommandBuffer& cmd,
                const std::span<T>& vectorData,
                const vk::ImageCreateInfo& _imageInfo,
                vk::ImageLayout finalLayout,
                uint32_t layerCount = 1,
                vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor)
            {
                // Create staging buffer and upload data
                Buffer stagingBuffer = createStagingBuffer(vectorData);

                // Create image in GPU memory
                vk::ImageCreateInfo imageInfo = _imageInfo;
                imageInfo.usage |= vk::ImageUsageFlagBits::eTransferDst; // We will copy data to this image
                Image image = createImage(imageInfo);

                // Transition image layout for copying data
                cmdTransitionImageLayout(cmd, image.image, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eTransferDstOptimal, aspectFlags, 0, layerCount);
                
                // Copy buffer data to the image
                std::vector<vk::BufferImageCopy> copyRegions(layerCount);
                
                VkDeviceSize faceSize = imageInfo.extent.width *
                                        imageInfo.extent.height *
                                        4; // assuming RGBA8 (4 bytes per pixel)

                for (uint32_t layer = 0; layer < layerCount; layer++)
                {
                    copyRegions[layer] = {
                        .bufferOffset = faceSize * layer,
                        .bufferRowLength = 0, // tightly packed
                        .bufferImageHeight = 0,
                        .imageSubresource = {
                            .aspectMask = aspectFlags,
                            .mipLevel = 0,
                            .baseArrayLayer = layer,
                            .layerCount = 1,
                        },
                        .imageOffset = {0, 0, 0},
                        .imageExtent = imageInfo.extent
                    };
                }
                cmd.copyBufferToImage(stagingBuffer.buffer, image.image, vk::ImageLayout::eTransferDstOptimal, copyRegions);
                
                // Transition image layout to final layout
                cmdTransitionImageLayout(cmd, image.image, vk::ImageLayout::eTransferDstOptimal, finalLayout, aspectFlags, 0, layerCount);

                ImageResource resultImage(image);
                resultImage.layout = finalLayout;
                return resultImage;
            }

            /*--
             * The staging buffers are buffers that are used to transfer data from the CPU to the GPU.
             * They cannot be freed until the data is transferred. So the command buffer must be completed, then the staging buffer can be cleared.
            -*/
            void freeStagingBuffers()
            {
                for (const auto& buffer : m_stagingBuffers)
                {
                    destroyBuffer(buffer);
                }
                m_stagingBuffers.clear();
            }

            /*-- When leak are reported, set the ID of the leak here --*/
            void setLeakID(uint32_t id) { m_leakID = id; }

        private:
            VmaAllocator m_allocator{};
            vk::Device m_device{};
            std::vector<Buffer> m_stagingBuffers{};
            uint32_t m_leakID = ~0U;
        };

        /*--
            * Return the path to a file if it exists in one of the search paths.
           -*/
        static std::string findFile(const std::string& filename, const std::vector<std::string>& searchPaths)
        {
            std::filesystem::path filePath = filename;
            if (filePath.is_absolute() && std::filesystem::exists(filePath))
            {
                std::string result = filePath.make_preferred().string();
                std::replace(result.begin(), result.end(), '\\', '/');
                return result;
            }

            for (const auto& path : searchPaths)
            {
                std::filesystem::path filePath = std::filesystem::path(path) / filename;
                if (std::filesystem::exists(filePath))
                {
                    // Convert path to string with forward slashes:
                    std::string result = filePath.make_preferred().string();
                    std::replace(result.begin(), result.end(), '\\', '/');
                    return result;
                }
            }
            VK_CORE_ERROR("File not found: %s", filename.c_str());
            /*LOGE("File not found: %s", filename.c_str());*/
            VK_CORE_INFO("Search under: ");
            /*LOGI("Search under: ");*/
            for (const auto& path : searchPaths)
            {
                VK_CORE_INFO("  %s", path.c_str());
                /*LOGI("  %s", path.c_str());*/
            }
            return "";
        }
    }
    /*--
     * Samplers are limited in Vulkan.
     * This class is used to create and store samplers, and to avoid creating the same sampler multiple times.
    -*/
    class SamplerPool
    {
    public:
        SamplerPool() = default;
        ~SamplerPool() = default;
        // Initialize the sampler pool with the device reference, then we can later acquire samplers
        void init(vk::raii::Device& device)
        {
            m_device = &device;
        }

        // Destroy internal resources and reset its initial state
        void deinit()
        {
            m_samplerMap.clear();
        }

        // Get or create VkSampler based on VkSamplerCreateInfo
        vk::raii::Sampler acquireSampler(const vk::SamplerCreateInfo& createInfo)
        {
            auto it = m_samplerMap.find(createInfo);
            if (it != m_samplerMap.end())
            {
                // If found, return existing sampler
                return std::move(it->second); // return existing
            }

            // Otherwise, create a new sampler
            auto [newIt, inserted] = m_samplerMap.try_emplace(
                createInfo, vk::raii::Sampler(*m_device, createInfo)
            );
            return std::move(newIt->second);
        }

        void releaseSampler(const vk::raii::Sampler& sampler)
        {
            for (auto it = m_samplerMap.begin(); it != m_samplerMap.end();)
            {
                if (&(it->second) == &sampler)
                {
                    it = m_samplerMap.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

    private:
        vk::raii::Device* m_device = nullptr;;

        struct SamplerCreateInfoHash
        {
            std::size_t operator()(const vk::SamplerCreateInfo& info) const
            {
                std::size_t seed = 0;
                auto hashCombine = [&](auto value)
                {
                    seed ^= std::hash<decltype(value)>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
                hashCombine(info.magFilter);
                hashCombine(info.minFilter);
                hashCombine(info.mipmapMode);
                hashCombine(info.addressModeU);
                hashCombine(info.addressModeV);
                hashCombine(info.addressModeW);
                hashCombine(info.mipLodBias);
                hashCombine(info.anisotropyEnable);
                hashCombine(info.maxAnisotropy);
                hashCombine(info.compareEnable);
                hashCombine(info.compareOp);
                hashCombine(info.minLod);
                hashCombine(info.maxLod);
                hashCombine(info.borderColor);
                hashCombine(info.unnormalizedCoordinates);
                return seed;
            }
        };

        struct SamplerCreateInfoEqual
        {
            bool operator()(const vk::SamplerCreateInfo& lhs, const vk::SamplerCreateInfo& rhs) const
            {
                return std::memcmp(&lhs, &rhs, sizeof(vk::SamplerCreateInfo)) == 0;
            }
        };

        // Stores unique samplers with their corresponding VkSamplerCreateInfo
        std::unordered_map<vk::SamplerCreateInfo, vk::raii::Sampler, SamplerCreateInfoHash, SamplerCreateInfoEqual>
        m_samplerMap;

        // Internal function to create a new VkSampler
        vk::raii::Sampler createSampler(const vk::SamplerCreateInfo& createInfo)
        {
            return vk::raii::Sampler(*m_device, createInfo);
        }
    };
    
    /*const std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
    };
    
    const std::vector<uint16_t> indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4
    };*/
    

    class VulkanRendererAPI : public RendererAPI
    {
    public:
        VulkanRendererAPI();
        explicit VulkanRendererAPI(const Config& config);
        ~VulkanRendererAPI() override;
        static VulkanRendererAPI& Get();

        void init()
        {
            initVulkan();

            // Acquiring the sampler which will be used for displaying the GBuffer
            const vk::SamplerCreateInfo info{.magFilter = vk::Filter::eLinear, .minFilter = vk::Filter::eLinear};
            linearSampler = m_samplerPool.acquireSampler(info);
            initImGui(); // todo remove from here because if you dont need it dont init
        }
    private:
        uint32_t AddTextureToPool(utils::ImageResource&& imageResource);
        void RemoveTextureFromPool(uint32_t index);
        VanKPipeLine createGraphicsPipeline(VanKGraphicsPipelineSpecification pipelineSpecification) override;
        void DestroyAllPipelines() override;
        void DestroyPipeline(VanKPipeLine pipeline) override;
        VanKCommandBuffer BeginCommandBuffer() override;
        void EndCommandBuffer(VanKCommandBuffer cmd) override;
        void BeginFrame() override;
        void EndFrame() override;
        void BindPipeline(VanKCommandBuffer cmd, VanKPipelineBindPoint pipelineBindPoint, VanKPipeLine pipeline) override;
        void BindUniformBuffer(VanKCommandBuffer cmd, VanKPipelineBindPoint bindPoint, UniformBuffer* buffer, uint32_t set, uint32_t binding, uint32_t arrayElement) override;
        void BeginRendering(VanKCommandBuffer cmd, const VanKColorTargetInfo* color_target_info, uint32_t num_color_targets, VanKDepthStencilTargetInfo depth_stencil_target_info, VanKRenderOption render_option) override;
        void SetViewport(VanKCommandBuffer cmd, uint32_t viewportCount, VanKViewport viewport) override;
        void SetScissor(VanKCommandBuffer cmd, uint32_t scissorCount, VankRect scissor) override;
        void BindVertexBuffer(VanKCommandBuffer cmd, uint32_t first_slot, const VertexBuffer& vertexBuffer, uint32_t num_bindings) override;
        void BindIndexBuffer(VanKCommandBuffer cmd, const IndexBuffer& indexBuffer, VanKIndexElementSize elementSize) override;
        void DrawIndexed(VanKCommandBuffer cmd, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;
        void EndRendering(VanKCommandBuffer cmd) override;
        void BindFragmentSamplers(VanKCommandBuffer cmd, uint32_t firstSlot, const TextureSamplerBinding* samplers, uint32_t num_bindings) override;
        /*-- Wait until GPU is done using the pipeline to safly destroy --*/
        void waitForGraphicsQueueIdle() override;
    public:
        struct PipelineResource
        {
            vk::raii::Pipeline pipeline = VK_NULL_HANDLE;
            vk::raii::PipelineLayout layout = VK_NULL_HANDLE;
            VanKPipelineBindPoint bindPoint;
            VanKGraphicsPipelineSpecification spec;
            VanKComputePipelineSpecification computeSpec;
        };
        std::unordered_map<vk::Pipeline, PipelineResource> m_PipelineResources;

        vk::PipelineLayout m_currentGraphicPipelineLayout;
        vk::PipelineLayout m_currentComputePipelineLayout;

        void RebuildSwapchain(bool vSyncVal) { vSync = vSyncVal; recreateSwapChain(); };
        void setFramebufferResized(bool resized) { framebufferResized = resized; }
        uint32_t getAPIVersion() const { return apiVersion; };
        vk::raii::Device& GetDevice() { return device; }
        utils::ResourceAllocator& GetAllocator() { return allocator; }
        ImTextureID getImTextureID(uint32_t index = 0) const override { return reinterpret_cast<ImTextureID>(uiDescriptorSet[index]); }
        void setViewportSize(Extent2D viewportSize) override
        { viewport = vk::Extent2D{viewportSize.width, viewportSize.height}; recreateImages(); }
    private:
        inline static VulkanRendererAPI* s_instance = nullptr;
        SDL_Window* window = nullptr;
        vk::raii::Context context;
        vk::raii::Instance instance = nullptr;
        uint32_t apiVersion = 0;
        vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
        vk::raii::SurfaceKHR surface = nullptr;
        vk::raii::PhysicalDevice physicalDevice = nullptr;
        vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
        vk::raii::Device device = nullptr;
        uint32_t queueIndex = ~0;
        vk::raii::Queue queue = nullptr;
        utils::ResourceAllocator allocator;
        vk::raii::SwapchainKHR swapChain = nullptr;
        std::vector<vk::Image> swapChainImages;
        vk::SurfaceFormatKHR swapChainSurfaceFormat;
        vk::Extent2D swapChainExtent;
        std::vector<vk::raii::ImageView> swapChainImageViews;

        vk::raii::PipelineLayout* pipelineLayout = nullptr;
        vk::raii::Pipeline* graphicsPipeline = nullptr;

        std::vector<utils::ImageResource> images;
        vk::Extent2D viewport;
        vk::raii::Image sceneImage = nullptr;
        vk::raii::DeviceMemory sceneImageMemory = nullptr;
        vk::raii::ImageView sceneImageView = nullptr;

        vk::raii::Image colorImage = nullptr;
        vk::raii::DeviceMemory colorImageMemory = nullptr;
        vk::raii::ImageView colorImageView = nullptr;

        vk::raii::Image depthImage = nullptr;
        vk::raii::DeviceMemory depthImageMemory = nullptr;
        vk::raii::ImageView depthImageView = nullptr;

        uint32_t mipLevels = 0;
        vk::raii::Image textureImage = nullptr;
        vk::raii::DeviceMemory textureImageMemory = nullptr;
        vk::raii::ImageView textureImageView = nullptr;
        SamplerPool m_samplerPool;
        vk::raii::Sampler linearSampler = nullptr;
        vk::raii::Sampler textureSampler = nullptr;
        vk::Format textureImageFormat = vk::Format::eUndefined;
        
        vk::raii::DescriptorPool descriptorPool = nullptr;
        vk::raii::DescriptorPool uiDescriptorPool = nullptr; // imgui 
        std::vector<vk::raii::DescriptorSet> descriptorSets;
        std::vector<VkDescriptorSet> uiDescriptorSet{}; // imgui
        vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
        vk::raii::DescriptorSetLayout commonDescriptorSetLayout = nullptr;

        vk::raii::CommandPool commandPool = nullptr;
        std::vector<vk::raii::CommandBuffer> commandBuffers;
        uint32_t currentImageIndex = {};
        vk::Result currentResult = {};
        
        std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
        std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
        std::vector<vk::raii::Fence> inFlightFences;
        uint32_t currentFrame = 0;

        bool framebufferResized = false;
        bool vSync = false;
        bool sceneImageInitialized = false;
        VanKRenderOption m_renderOption = {};

        std::vector<const char*> requiredDeviceExtension =
        {
            vk::KHRSwapchainExtensionName,
            vk::KHRSpirv14ExtensionName,
            vk::KHRSynchronization2ExtensionName,
            vk::KHRCreateRenderpass2ExtensionName,
            vk::EXTDescriptorIndexingExtensionName
        };

        void initVulkan();

        void initImGui();

        void mainLoop(VanKCommandBuffer cmd);

        void cleanupSwapChain();

        void cleanup();

        void recreateSwapChain();
        
        void recreateImages();

        void createInstance();

        void setupDebugMessenger();

        void createSurface();

        void pickPhysicalDevice();

        void createLogicalDevice();

        void createDynamicDispatcher();

        void createSwapChain();

        //change this ? from chapter image view
        void createImageViews();

        void createCommandPool();

        void createSceneResources();

        void createColorResources();

        void createDepthResources();

        vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling,
                                       vk::FormatFeatureFlags features) const;

        [[nodiscard]] vk::Format findDepthFormat() const;

        static bool hasStencilComponent(vk::Format format);

        void createTexture();

        void generateMipmaps(vk::raii::Image& image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight,
                             uint32_t mipLevels);

        vk::SampleCountFlagBits getMaxUsableSampleCount();

        void createTextureSampler();

        vk::raii::ImageView createImageView(vk::raii::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags,
                                            uint32_t mipLevels);

        void createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples,
                         vk::Format format, vk::ImageTiling tiling,
                         vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image,
                         vk::raii::DeviceMemory& imageMemory);

        void transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                   uint32_t mipLevels);

        /*void copyBufferToImage(const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height,
                               uint64_t offset = 0, uint32_t mipLevel = 0);*/
        
        void createDescriptorPool();

        void createDescriptorSets();
        void updateGraphicsDescriptorSet();

        /*void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties,
                          vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory);*/

        std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();

        void endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const;

        /*//change this to use the above functions check images chapter or 1 before idk
        void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size);*/

        uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

        void createCommandBuffers();
        
        void transition_image_layout(
            uint32_t imageIndex,
            vk::ImageLayout old_layout,
            vk::ImageLayout new_layout,
            vk::AccessFlags2 src_access_mask,
            vk::AccessFlags2 dst_access_mask,
            vk::PipelineStageFlags2 src_stage_mask,
            vk::PipelineStageFlags2 dst_stage_mask
        );

        void transition_image_layout_custom(
            vk::raii::Image& image,
            vk::ImageLayout old_layout,
            vk::ImageLayout new_layout,
            vk::AccessFlags2 src_access_mask,
            vk::AccessFlags2 dst_access_mask,
            vk::PipelineStageFlags2 src_stage_mask,
            vk::PipelineStageFlags2 dst_stage_mask,
            vk::ImageAspectFlags aspect_mask
        );

        void createSyncObjects();

        /*[[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;*/

        static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities);

        static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);

        static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes,
                                                        bool vsync);

        vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

        std::vector<const char*> getRequiredExtensions();

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                              vk::DebugUtilsMessageTypeFlagsEXT type,
                                                              const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                              void*);

        static std::vector<char> readFile(const std::string& filename);
    };

    inline vk::Format ConvertToVkColorFormat(VanKFormat format)
    {
        switch (format)
        {
        case VanK_FORMAT_INVALID: return vk::Format::eUndefined;
        case VanK_Format_B8G8R8A8Srgb: return vk::Format::eB8G8R8A8Srgb;
        case VanK_FORMAT_R32_SINT: return vk::Format::eR32Sint;
        
        }
        return vk::Format::eUndefined;
    }

    inline vk::PrimitiveTopology ConvertToVkPrimitiveTopology(VankPrimitiveToplogy toplogy)
    {
        switch (toplogy)
        {
        case VanK_PRIMITIVE_TOPOLOGY_POINT_LIST: return vk::PrimitiveTopology::ePointList;
        case VanK_PRIMITIVE_TOPOLOGY_LINE_LIST: return vk::PrimitiveTopology::eLineList;
        case VanK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return vk::PrimitiveTopology::eLineStrip;
        case VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return vk::PrimitiveTopology::eTriangleList;
        case VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return vk::PrimitiveTopology::eTriangleStrip;
        case VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return vk::PrimitiveTopology::eTriangleFan;
        case VanK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return
                vk::PrimitiveTopology::eLineListWithAdjacency;
        case VanK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return
                vk::PrimitiveTopology::eLineStripWithAdjacency;
        case VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return
                vk::PrimitiveTopology::eTriangleListWithAdjacency;
        case VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return
                vk::PrimitiveTopology::eTriangleStripWithAdjacency;
        case VanK_PRIMITIVE_TOPOLOGY_PATCH_LIST: return vk::PrimitiveTopology::ePatchList;
        }

        return vk::PrimitiveTopology::eTriangleList;
    }

    inline vk::FrontFace ConvertToVkFrontFace(VanKFrontFace frontFace)
    {
        switch (frontFace)
        {
        case VanK_FRONT_FACE_COUNTER_CLOCKWISE: return vk::FrontFace::eCounterClockwise;
        case VanK_FRONT_FACE_CLOCKWISE: return vk::FrontFace::eClockwise;
        }
        return vk::FrontFace::eCounterClockwise;
    }

    inline vk::CullModeFlags ConvertToVkCullMode(VanKCullModeFlags cullMode)
    {
        switch (cullMode)
        {
        case VanK_CULL_MODE_NONE: return vk::CullModeFlagBits::eNone;
        case VanK_CULL_MODE_FRONT_BIT: return vk::CullModeFlagBits::eFront;
        case VanK_CULL_MODE_BACK_BIT: return vk::CullModeFlagBits::eBack;
        case VanK_CULL_MODE_FRONT_AND_BACK: return vk::CullModeFlagBits::eFrontAndBack;
        }
        return vk::CullModeFlagBits::eNone;
    }

    inline vk::PolygonMode ConvertToVkPolygonMode(VanKPolygonMode polygonMode)
    {
        switch (polygonMode)
        {
        case VanK_POLYGON_MODE_FILL: return vk::PolygonMode::eFill;
        case VanK_POLYGON_MODE_LINE: return vk::PolygonMode::eLine;
        case VanK_POLYGON_MODE_POINT: return vk::PolygonMode::ePoint;
        case VanK_POLYGON_MODE_FILL_RECTANGLE_NV: return vk::PolygonMode::eFillRectangleNV;
        }
        return vk::PolygonMode::eFill;
    }

    inline vk::BlendFactor ConvertToVkBlendFactor(VanKBlendFactor blendFactor)
        {
            switch (blendFactor)
            {
            case VanK_BLEND_FACTOR_ZERO: return vk::BlendFactor::eZero;
                case VanK_BLEND_FACTOR_ONE: return vk::BlendFactor::eOne;
                case VanK_BLEND_FACTOR_SRC_COLOR: return vk::BlendFactor::eSrcColor;
                case VanK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return vk::BlendFactor::eOneMinusSrcColor;
                case VanK_BLEND_FACTOR_DST_COLOR: return vk::BlendFactor::eDstColor;
                case VanK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return vk::BlendFactor::eOneMinusDstColor;
                case VanK_BLEND_FACTOR_SRC_ALPHA: return vk::BlendFactor::eSrcAlpha;
                case VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return vk::BlendFactor::eOneMinusSrcAlpha;
                case VanK_BLEND_FACTOR_DST_ALPHA: return vk::BlendFactor::eDstAlpha;
                case VanK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return vk::BlendFactor::eOneMinusDstAlpha;
                case VanK_BLEND_FACTOR_CONSTANT_COLOR: return vk::BlendFactor::eConstantColor;
                case VanK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return vk::BlendFactor::eOneMinusConstantColor;
                case VanK_BLEND_FACTOR_CONSTANT_ALPHA: return vk::BlendFactor::eConstantAlpha;
                case VanK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return vk::BlendFactor::eOneMinusConstantAlpha;
                case VanK_BLEND_FACTOR_SRC_ALPHA_SATURATE: return vk::BlendFactor::eSrcAlphaSaturate;
                case VanK_BLEND_FACTOR_SRC1_COLOR: return vk::BlendFactor::eSrc1Color;
                case VanK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR: return vk::BlendFactor::eOneMinusSrc1Color;
                case VanK_BLEND_FACTOR_SRC1_ALPHA: return vk::BlendFactor::eSrc1Alpha;
                case VanK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA: return vk::BlendFactor::eOneMinusSrc1Alpha;
            }
            return vk::BlendFactor::eZero;
        }

    inline vk::BlendOp ConvertToVkBlendOp(VanKBlendOp blendOp)
        {
            switch (blendOp)
            {
                case VanK_BLEND_OP_ADD: return vk::BlendOp::eAdd;
                case VanK_BLEND_OP_SUBTRACT: return vk::BlendOp::eSubtract;
                case VanK_BLEND_OP_REVERSE_SUBTRACT: return vk::BlendOp::eReverseSubtract;
                case VanK_BLEND_OP_MIN: return vk::BlendOp::eMin;
                case VanK_BLEND_OP_MAX: return vk::BlendOp::eMax;
                case VanK_BLEND_OP_ZERO_EXT: return vk::BlendOp::eZeroEXT;
                case VanK_BLEND_OP_SRC_EXT: return vk::BlendOp::eSrcEXT;
                case VanK_BLEND_OP_DST_EXT: return vk::BlendOp::eDstEXT;
                case VanK_BLEND_OP_SRC_OVER_EXT: return vk::BlendOp::eSrcOverEXT;
                case VanK_BLEND_OP_DST_OVER_EXT: return vk::BlendOp::eDstOverEXT;
                case VanK_BLEND_OP_SRC_IN_EXT: return vk::BlendOp::eSrcInEXT;
                case VanK_BLEND_OP_DST_IN_EXT: return vk::BlendOp::eDstInEXT;
                case VanK_BLEND_OP_SRC_OUT_EXT: return vk::BlendOp::eSrcOutEXT;
                case VanK_BLEND_OP_DST_OUT_EXT: return vk::BlendOp::eDstOutEXT;
                case VanK_BLEND_OP_SRC_ATOP_EXT: return vk::BlendOp::eSrcAtopEXT;
                case VanK_BLEND_OP_DST_ATOP_EXT: return vk::BlendOp::eDstAtopEXT;
                case VanK_BLEND_OP_XOR_EXT: return vk::BlendOp::eXorEXT;
                case VanK_BLEND_OP_MULTIPLY_EXT: return vk::BlendOp::eMultiplyEXT;
                case VanK_BLEND_OP_SCREEN_EXT: return vk::BlendOp::eScreenEXT;
                case VanK_BLEND_OP_OVERLAY_EXT: return vk::BlendOp::eOverlayEXT;
                case VanK_BLEND_OP_DARKEN_EXT: return vk::BlendOp::eDarkenEXT;
                case VanK_BLEND_OP_LIGHTEN_EXT: return vk::BlendOp::eLightenEXT;
                case VanK_BLEND_OP_COLORDODGE_EXT: return vk::BlendOp::eColordodgeEXT;
                case VanK_BLEND_OP_COLORBURN_EXT: return vk::BlendOp::eColorburnEXT;
                case VanK_BLEND_OP_HARDLIGHT_EXT: return vk::BlendOp::eHardlightEXT;
                case VanK_BLEND_OP_SOFTLIGHT_EXT: return vk::BlendOp::eSoftlightEXT;
                case VanK_BLEND_OP_DIFFERENCE_EXT: return vk::BlendOp::eDifferenceEXT;
                case VanK_BLEND_OP_EXCLUSION_EXT: return vk::BlendOp::eExclusionEXT;
                case VanK_BLEND_OP_INVERT_EXT: return vk::BlendOp::eInvertEXT;
                case VanK_BLEND_OP_INVERT_RGB_EXT: return vk::BlendOp::eInvertRgbEXT;
                case VanK_BLEND_OP_LINEARDODGE_EXT: return vk::BlendOp::eLineardodgeEXT;
                case VanK_BLEND_OP_LINEARBURN_EXT: return vk::BlendOp::eLinearburnEXT;
                case VanK_BLEND_OP_VIVIDLIGHT_EXT: return vk::BlendOp::eVividlightEXT;
                case VanK_BLEND_OP_LINEARLIGHT_EXT: return vk::BlendOp::eLinearlightEXT;
                case VanK_BLEND_OP_PINLIGHT_EXT: return vk::BlendOp::ePinlightEXT;
                case VanK_BLEND_OP_HARDMIX_EXT: return vk::BlendOp::eHardmixEXT;
                case VanK_BLEND_OP_HSL_HUE_EXT: return vk::BlendOp::eHslHueEXT;
                case VanK_BLEND_OP_HSL_SATURATION_EXT: return vk::BlendOp::eHslSaturationEXT;
                case VanK_BLEND_OP_HSL_COLOR_EXT: return vk::BlendOp::eHslColorEXT;
                case VanK_BLEND_OP_HSL_LUMINOSITY_EXT: return vk::BlendOp::eHslLuminosityEXT;
                case VanK_BLEND_OP_PLUS_EXT: return vk::BlendOp::ePlusEXT;
                case VanK_BLEND_OP_PLUS_CLAMPED_EXT: return vk::BlendOp::ePlusClampedEXT;
                case VanK_BLEND_OP_PLUS_CLAMPED_ALPHA_EXT: return vk::BlendOp::ePlusClampedAlphaEXT;
                case VanK_BLEND_OP_PLUS_DARKER_EXT: return vk::BlendOp::ePlusDarkerEXT;
                case VanK_BLEND_OP_MINUS_EXT: return vk::BlendOp::eMinusEXT;
                case VanK_BLEND_OP_MINUS_CLAMPED_EXT: return vk::BlendOp::eMinusClampedEXT;
                case VanK_BLEND_OP_CONTRAST_EXT: return vk::BlendOp::eContrastEXT;
                case VanK_BLEND_OP_INVERT_OVG_EXT: return vk::BlendOp::eInvertOvgEXT;
                case VanK_BLEND_OP_RED_EXT: return vk::BlendOp::eRedEXT;
                case VanK_BLEND_OP_GREEN_EXT: return vk::BlendOp::eGreenEXT;
                case VanK_BLEND_OP_BLUE_EXT: return vk::BlendOp::eBlueEXT;
            }
            return vk::BlendOp::eMin;
        }

    inline vk::ColorComponentFlags ConvertToVkcolorWriteMask(VanKColorComponentFlags colorWriteMask)
    {
        vk::ColorComponentFlags flags = {};
        if (colorWriteMask & VanK_COLOR_COMPONENT_R_BIT) flags |= vk::ColorComponentFlagBits::eR;
        if (colorWriteMask & VanK_COLOR_COMPONENT_G_BIT) flags |= vk::ColorComponentFlagBits::eG;
        if (colorWriteMask & VanK_COLOR_COMPONENT_B_BIT) flags |= vk::ColorComponentFlagBits::eB;
        if (colorWriteMask & VanK_COLOR_COMPONENT_A_BIT) flags |= vk::ColorComponentFlagBits::eA;
        return flags;
    }


    inline vk::CompareOp ConvertToVkCompareOp(VanKdepthCompareOp compareOp)
    {
        switch (compareOp)
        {
        case VanK_COMPARE_OP_NEVER: return vk::CompareOp::eNever;
        case VanK_COMPARE_OP_LESS: return vk::CompareOp::eLess;
        case VanK_COMPARE_OP_EQUAL: return vk::CompareOp::eEqual;
        case VanK_COMPARE_OP_LESS_OR_EQUAL: return vk::CompareOp::eLessOrEqual;
        case VanK_COMPARE_OP_GREATER: return vk::CompareOp::eGreater;
        case VanK_COMPARE_OP_NOT_EQUAL: return vk::CompareOp::eNotEqual;
        case VanK_COMPARE_OP_GREATER_OR_EQUAL: return vk::CompareOp::eGreaterOrEqual;
        case VanK_COMPARE_OP_ALWAYS: return vk::CompareOp::eAlways;
        }
        return vk::CompareOp::eLess;
    }
}