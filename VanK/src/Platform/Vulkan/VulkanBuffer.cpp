#include "VulkanBuffer.h"

#include "VanK/Core/core.h"
#include "VanK/Renderer/RenderCommand.h"
#include "VanK/Renderer/Renderer.h"

namespace VanK
{
    VulkanVanKBuffer::VulkanVanKBuffer(uint64_t size)
    {
    }

    VulkanVanKBuffer::~VulkanVanKBuffer()
    {
    }

    void VulkanVanKBuffer::Bind() const
    {
    }

    void VulkanVanKBuffer::Unbind() const
    {
    }

    VulkanVertexBuffer::VulkanVertexBuffer(uint64_t size) : m_size(size)
    {
        VK_CORE_INFO("Created VertexBuffer");
        auto& instance = VulkanRendererAPI::Get();

        m_vertexBuffer = instance.GetAllocator().createBuffer
        (
            size,
            vk::BufferUsageFlagBits2::eVertexBuffer | vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            vma::MemoryUsage::eGpuOnly
        );
        DBG_VK_NAME(m_vertexBuffer.buffer);
        TracyAllocN(*m_vertexBuffer.buffer, size, "Vulkan VRAM");
    }

    VulkanVertexBuffer::~VulkanVertexBuffer()
    {
        VK_CORE_INFO("Destroyed VertexBuffer");
        /*auto& instance = VulkanRendererAPI::Get();
        
        instance.GetAllocator().destroyBuffer(m_vertexBuffer); // not needed raii*/
        TracyFreeN(*m_vertexBuffer.buffer, "Vulkan VRAM");
    }

    void VulkanVertexBuffer::Bind() const
    {
    }

    void VulkanVertexBuffer::Unbind() const
    {
    }

    void VulkanVertexBuffer::Upload(const void* data, size_t size)
    {
    }

    VulkanIndexBuffer::VulkanIndexBuffer(uint64_t size) : m_size(size), m_Count(static_cast<uint32_t>(size / sizeof(uint32_t))) // Calculate count from size
    {
        VK_CORE_INFO("Created IndexBuffer");
        auto& instance = VulkanRendererAPI::Get();

        m_indexBuffer = instance.GetAllocator().createBuffer
        (
            size,
            vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            vma::MemoryUsage::eGpuOnly
        );
        DBG_VK_NAME(m_indexBuffer.buffer);
        TracyAllocN(*m_indexBuffer.buffer, size, "Vulkan VRAM");
    }

    VulkanIndexBuffer::~VulkanIndexBuffer()
    {
        VK_CORE_INFO("Destroyed IndexBuffer");
        /*auto& instance = VulkanRendererAPI::Get();
        
        instance.GetAllocator().destroyBuffer(m_indexBuffer); // not needed raii*/
        TracyFreeN(*m_indexBuffer.buffer, "Vulkan VRAM");
    }

    void VulkanIndexBuffer::Bind() const
    {
    }

    void VulkanIndexBuffer::Unbind() const
    {
    }

    void VulkanIndexBuffer::Upload(const void* data, size_t size, size_t offset)
    {
    }

    VulkanTransferBuffer::VulkanTransferBuffer(uint64_t size, VanKTransferBufferUsage usage) : m_size(size)
    {
        VK_CORE_INFO("Created TransferBuffer");
        auto& instance = VulkanRendererAPI::Get();

        vma::MemoryUsage memoryUsage = {};

        switch (usage)
        {
        case VanKTransferBufferUsageUpload: memoryUsage = vma::MemoryUsage::eCpuToGpu;
            break;
        case VanKTransferBufferUsageDownload: memoryUsage = vma::MemoryUsage::eGpuToCpu;
            break;
        }

        m_transferBuffer = instance.GetAllocator().createBuffer
        (
            size,
            vk::BufferUsageFlagBits2::eTransferSrc | vk::BufferUsageFlagBits2::eTransferDst,
            memoryUsage,
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite
        );
        DBG_VK_NAME(m_transferBuffer.buffer);
        TracyAllocN(*m_transferBuffer.buffer, size, "Vulkan VRAM");
    }

    VulkanTransferBuffer::~VulkanTransferBuffer()
    {
        VK_CORE_INFO("Destroyed TransferBuffer");
        RenderCommand::waitForGraphicsQueueIdle();
        /*auto& instance = VulkanRendererAPI::Get();
        
        instance.GetAllocator().destroyBuffer(m_transferBuffer); // not needed raii*/
        TracyFreeN(*m_transferBuffer.buffer, "Vulkan VRAM");
    }

    void VulkanTransferBuffer::Bind() const
    {
    }

    void VulkanTransferBuffer::Unbind() const
    {
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

    void* VulkanTransferBuffer::MapTransferBuffer(uint64_t size, uint64_t alignment, uint64_t& outOffset)
    {
        //ring buffer offset

        // --- check if request itself is too large ---
        if (size > m_size)
        {
            VK_CORE_ERROR("VulkanTransferBuffer::MapTransferBuffer Requested transfer size ({0}) exceeds transfer buffer size ({1})!", size, m_size);
            VK_CORE_ASSERT(false, "Transfer size too large!");
        }

        // Calculate aligned offset without committing
        vk::DeviceSize alignedOffset = (m_currentOffset + alignment - 1) & ~(alignment - 1);

        // Check if space from current offset to end is enough
        if (alignedOffset + size > m_size)
        {
            // wrap around
            alignedOffset = 0;

            // If wrapped space is still not enough, fail early
            if (size > m_currentOffset)
            {
                VK_CORE_ERROR("Not enough space in transfer buffer. Requested {0}, buffer size {1}, current offset {2}", size, m_size, m_currentOffset);
                VK_CORE_ASSERT(false, "Transfer buffer overflow!");
                return nullptr;
            }
        }

        // Only map memory now that we know there is enough space
        // Map and copy data to the staging buffer
        try
        {
            void* mappedPtr = m_transferBuffer.buffer.getAllocation().map();

            outOffset = alignedOffset;
            m_currentOffset = alignedOffset + size; // now commit

            return static_cast<uint8_t*>(mappedPtr) + alignedOffset;
        }
        catch ([[maybe_unused]] const vk::SystemError& err)
        {
            VK_CORE_ERROR("Failed to map transfer buffer memory!");
            return nullptr;
        }
    }

    void VulkanTransferBuffer::UnMapTransferBuffer()
    {
        m_transferBuffer.buffer.getAllocation().unmap();
    }

    void VulkanTransferBuffer::UploadToGPUBufferRAW(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion, bool runtime)
    {
        if (bufferRegion.size == 0)
            return; // or skip the copy safely

        if (bufferRegion.buffer == VK_NULL_HANDLE)
        {
            std::cerr << "Error: bufferRegion.buffer is null!" << '\n';
            return;
        }

        if (runtime)
        {
            // Add a barrier to make sure nothing was writing to it, before updating its content
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(cmd),
                static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR
            );

            vk::BufferCopy2 copyRegion;
            copyRegion.srcOffset = location.offset;
            copyRegion.dstOffset = bufferRegion.offset;
            copyRegion.size = bufferRegion.size;
            //maybe copyregion array idk

            const vk::CopyBufferInfo2 copyBufferInfo
            {
                .srcBuffer = m_transferBuffer.buffer,
                .dstBuffer = static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
                .regionCount = 1,
                .pRegions = &copyRegion
            };

            Unwrap(cmd).copyBuffer2(copyBufferInfo);

            // Add barrier to make sure the buffer is updated before the fragment shader uses it
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(cmd),
                static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eTransfer,
                vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eTransferWrite,
                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR
            );
        }
        else
        {
            auto& instance = VulkanRendererAPI::Get();
            auto command = utils::beginSingleTimeCommands(instance.GetDevice(), instance.GetCommandPool());
            // Add a barrier to make sure nothing was writing to it, before updating its content
            utils::cmdBufferMemoryBarrier
            (
                *command,
                static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR
            );

            vk::BufferCopy2 copyRegion;
            copyRegion.srcOffset = location.offset;
            copyRegion.dstOffset = bufferRegion.offset;
            copyRegion.size = bufferRegion.size;
            //maybe copyregion array idk

            const vk::CopyBufferInfo2 copyBufferInfo
            {
                .srcBuffer = m_transferBuffer.buffer,
                .dstBuffer = static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
                .regionCount = 1,
                .pRegions = &copyRegion
            };

            command->copyBuffer2(copyBufferInfo);

            // Add barrier to make sure the buffer is updated before the fragment shader uses it
            utils::cmdBufferMemoryBarrier
            (
                *command,
                static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eTransfer,
                vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eTransferWrite,
                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR
            );
            utils::endSingleTimeCommands(*command, instance.GetQueue());
        }
    }

    void VulkanTransferBuffer::DownloadFromGPUBufferRAW(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion)
    {
        if (bufferRegion.size == 0)
            return; // or skip the copy safely

        if (bufferRegion.buffer == VK_NULL_HANDLE)
        {
            std::cerr << "Error: bufferRegion.buffer is null!" << '\n';
            return;
        }

        // Add a barrier to make sure nothing was writing to it, before updating its content
        utils::cmdBufferMemoryBarrier
        (
            Unwrap(cmd),
            static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
            vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTaskShaderEXT |
            vk::PipelineStageFlagBits2::eMeshShaderEXT,
            vk::PipelineStageFlagBits2::eTransfer
        );

        vk::BufferCopy2 copyRegion;
        copyRegion.srcOffset = location.offset;
        copyRegion.dstOffset = bufferRegion.offset;
        copyRegion.size = bufferRegion.size;
        //maybe copyregion array idk

        const vk::CopyBufferInfo2 copyBufferInfo
        {
            .srcBuffer = static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
            .dstBuffer = m_transferBuffer.buffer,
            .regionCount = 1,
            .pRegions = &copyRegion
        };

        Unwrap(cmd).copyBuffer2(copyBufferInfo);

        // Add barrier to make sure the buffer is updated before the fragment shader uses it
        utils::cmdBufferMemoryBarrier
        (
            Unwrap(cmd),
            static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
            vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eTaskShaderEXT |
            vk::PipelineStageFlagBits2::eMeshShaderEXT
        );

        utils::cmdBufferMemoryBarrier
        (
            Unwrap(cmd),
            m_transferBuffer.buffer,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eHost
        );
    }
    
    void VulkanTransferBuffer::UploadRaw(VanKCommandBuffer& cmd, VanKBuffer& dstBuffer, const void* vecData, uint64_t dataSize, uint64_t alignment, uint64_t dstOffset, bool runtime)
    {
        uint64_t offset = 0;
        
        void* dataPtr = MapTransferBuffer(dataSize, alignment, offset);
        
        memcpy(dataPtr, vecData, dataSize);
        
        m_transferBuffer.buffer.getAllocation().flush(offset, dataSize); // added my self idk if needed
        
        UnMapTransferBuffer();
        
        UploadToGPUBufferRAW(cmd, VanKTransferBufferLocation{.offset = offset}, VanKBufferRegion{.buffer = &dstBuffer, .offset = dstOffset, .size = dataSize}, runtime);
    }
    
    void VulkanTransferBuffer::DownloadRaw(VanKCommandBuffer& cmd, VanKBuffer& srcBuffer, void* outData, uint64_t dataSize, uint64_t alignment, uint64_t srcOffset)
    {
        uint64_t offset = 0;
        
        DownloadFromGPUBufferRAW(cmd, VanKTransferBufferLocation{.offset = offset}, VanKBufferRegion{.buffer = &srcBuffer, .offset = srcOffset, .size = dataSize});
        
        void* mappedPtr = MapTransferBuffer(dataSize, alignment, offset);
        
        m_transferBuffer.buffer.getAllocation().invalidate(offset, dataSize);
        
        memcpy(outData, static_cast<uint8_t*>(mappedPtr) + offset, dataSize);
        
        UnMapTransferBuffer();
    }

    void VulkanTransferBuffer::DownloadFromGPUImage(VanKCommandBuffer& cmd, uint32_t srcImage, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    { 
        /*// Clamp coordinates to viewport
        x = std::min(x, width - 1); // viewport
        y = std::min(y, height - 1); // viewport*/
        
        vk::BufferImageCopy2 copyRegion;
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.imageOffset = vk::Offset3D{static_cast<int>(x), static_cast<int>(y), 0};
        copyRegion.imageExtent = vk::Extent3D{1, 1, 1}; // Always 1x1 for single pixel picking
        
        auto& instance = VulkanRendererAPI::Get();
        
        vk::CopyImageToBufferInfo2 copyInfo;
        copyInfo.srcImage = instance.GetRenderTargetImage(srcImage).image;
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstBuffer = m_transferBuffer.buffer;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &copyRegion;
        
        Unwrap(cmd).copyImageToBuffer2(copyInfo);
    }
    
    int32_t VulkanTransferBuffer::ReadPixel(uint32_t x, uint32_t y)
    {
        // We don't need a command buffer here because we are just reading CPU memory
        void* mappedPtr = m_transferBuffer.buffer.getAllocation().map();
    
        // Invalidate ensures the CPU doesn't use an old cached version of this memory
        m_transferBuffer.buffer.getAllocation().invalidate(0, sizeof(int32_t));
    
        int32_t id = *static_cast<int32_t*>(mappedPtr);
    
        m_transferBuffer.buffer.getAllocation().unmap();
    
        return id;
    }
    
    VulkanUniformBuffer::VulkanUniformBuffer(uint64_t size) : m_size(size)
    {
        VK_CORE_INFO("Created UniformBuffer");
        auto& instance = VulkanRendererAPI::Get();

        m_uniformBuffer = instance.GetAllocator().createBuffer
        (
            size,
            vk::BufferUsageFlagBits2::eUniformBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            vma::MemoryUsage::eGpuOnly
        );
        DBG_VK_NAME(m_uniformBuffer.buffer);
        TracyAllocN(*m_uniformBuffer.buffer, size, "Vulkan VRAM");
    }

    VulkanUniformBuffer::~VulkanUniformBuffer()
    {
        VK_CORE_INFO("Destroyed UniformBuffer");
        /*auto& instance = VulkanRendererAPI::Get();
        
        instance.GetAllocator().destroyBuffer(m_uniformBuffer); // not needed raii*/
        TracyFreeN(*m_uniformBuffer.buffer, "Vulkan VRAM");
    }

    void VulkanUniformBuffer::Bind() const
    {
    }

    void VulkanUniformBuffer::Unbind() const
    {
    }

    void VulkanUniformBuffer::Update(VanKCommandBuffer cmd, const void* data, size_t size)
    {
        // Add a memory barrier before updating (optional, but good practice)
        utils::cmdBufferMemoryBarrier
        (
            Unwrap(cmd),
            m_uniformBuffer.buffer,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eTransfer
        );

        // Update the buffer
        Unwrap(cmd).updateBuffer(m_uniformBuffer.buffer, 0, vk::ArrayProxy<const uint8_t>(static_cast<uint32_t>(size), static_cast<const uint8_t*>(data)));

        // Add a memory barrier after updating
        utils::cmdBufferMemoryBarrier
        (
            Unwrap(cmd),
            m_uniformBuffer.buffer,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eFragmentShader
        );
    }

    VulkanStorageBuffer::VulkanStorageBuffer(uint64_t size) : m_size(size)
    {
        VK_CORE_INFO("Created StorageBuffer");
        auto& instance = VulkanRendererAPI::Get();

        m_storageBuffer = instance.GetAllocator().createBuffer
        (
            size,
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferSrc | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress |
            vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR,
            vma::MemoryUsage::eGpuOnly
        );
        DBG_VK_NAME(m_storageBuffer.buffer);
        TracyAllocN(*m_storageBuffer.buffer, size, "Vulkan VRAM");
    }

    VulkanStorageBuffer::~VulkanStorageBuffer()
    {
        VK_CORE_INFO("Destroyed StorageBuffer");
        RenderCommand::waitForGraphicsQueueIdle();
        
        /*auto& instance = VulkanRendererAPI::Get();
        
        instance.GetAllocator().destroyBuffer(m_storageBuffer); // not needed raii*/
        TracyFreeN(*m_storageBuffer.buffer, "Vulkan VRAM");
    }

    void VulkanStorageBuffer::Bind() const
    {
    }

    void VulkanStorageBuffer::Unbind() const
    {
    }
    
    void VulkanStorageBuffer::Upload(const void* data, size_t size, size_t offset)
    {

    }

    void VulkanStorageBuffer::Fill(VanKCommandBuffer cmd, uint64_t dstOffset, uint64_t size, uint32_t data)
    {
        Unwrap(cmd).fillBuffer(m_storageBuffer.buffer, dstOffset, size, data);
    }

    VulkanIndirectBuffer::VulkanIndirectBuffer(uint64_t size) : m_size(size)
    {
        VK_CORE_INFO("Created IndirectBuffer");
        auto& instance = VulkanRendererAPI::Get();

        m_indirectBuffer = instance.GetAllocator().createBuffer
        (
            size,
            vk::BufferUsageFlagBits2::eIndirectBuffer | vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            vma::MemoryUsage::eCpuToGpu
        );
        DBG_VK_NAME(m_indirectBuffer.buffer);
        TracyAllocN(*m_indirectBuffer.buffer, size, "Vulkan VRAM");
    }

    VulkanIndirectBuffer::~VulkanIndirectBuffer()
    {
        VK_CORE_INFO("Destroyed IndirectBuffer");
        /*auto& instance = VulkanRendererAPI::Get();
        
        instance.GetAllocator().destroyBuffer(m_indirectBuffer); // not needed raii*/
        TracyFreeN(*m_indirectBuffer.buffer, "Vulkan VRAM");
    }

    void VulkanIndirectBuffer::Bind() const
    {
    }

    void VulkanIndirectBuffer::Unbind() const
    {
    }

    void VulkanIndirectBuffer::Upload(const void* data, size_t size, size_t offset)
    {
    }

    void VulkanIndirectBuffer::Fill(VanKCommandBuffer cmd, uint64_t dstOffset, uint64_t size, uint32_t data)
    {
        Unwrap(cmd).fillBuffer(m_indirectBuffer.buffer, dstOffset, size, data);
    }
}
