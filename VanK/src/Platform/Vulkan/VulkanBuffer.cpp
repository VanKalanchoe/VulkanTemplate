#include "VulkanBuffer.h"

#include "VanK/Core/core.h"

VanK::VulkanVanKBuffer::VulkanVanKBuffer(uint64_t size) {}

VanK::VulkanVanKBuffer::~VulkanVanKBuffer() {}

void VanK::VulkanVanKBuffer::Bind() const {}

void VanK::VulkanVanKBuffer::Unbind() const {}

VanK::VulkanVertexBuffer::VulkanVertexBuffer(uint64_t size)
{
    VK_CORE_INFO("Created VertexBuffer");
    auto& instance = VulkanRendererAPI::Get();

    m_vertexBuffer = instance.GetAllocator().createBuffer
    (
        size,
        vk::BufferUsageFlagBits2::eVertexBuffer | vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    DBG_VK_NAME(m_vertexBuffer.buffer);
}

VanK::VulkanVertexBuffer::~VulkanVertexBuffer()
{
    VK_CORE_INFO("Destroyed VertexBuffer");
    auto& instance = VulkanRendererAPI::Get();
    
    instance.GetAllocator().destroyBuffer(m_vertexBuffer);
}

void VanK::VulkanVertexBuffer::Bind() const
{
}

void VanK::VulkanVertexBuffer::Unbind() const
{
}

void VanK::VulkanVertexBuffer::Upload(const void* data, size_t size)
{
}

VanK::VulkanIndexBuffer::VulkanIndexBuffer(uint64_t size) : m_Count(size / sizeof(uint32_t))  // Calculate count from size
{
    VK_CORE_INFO("Created IndexBuffer");
    auto& instance = VulkanRendererAPI::Get();

    m_indexBuffer = instance.GetAllocator().createBuffer
    (
        size,
        vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    DBG_VK_NAME(m_indexBuffer.buffer);
}

VanK::VulkanIndexBuffer::~VulkanIndexBuffer()
{
    VK_CORE_INFO("Destroyed IndexBuffer");
    auto& instance = VulkanRendererAPI::Get();
    
    instance.GetAllocator().destroyBuffer(m_indexBuffer);
}

void VanK::VulkanIndexBuffer::Bind() const
{
}

void VanK::VulkanIndexBuffer::Unbind() const
{
}

void VanK::VulkanIndexBuffer::Upload(const void* data, size_t size, size_t offset)
{
}

VanK::VulkanTransferBuffer::VulkanTransferBuffer(uint64_t size, VanKTransferBufferUsage usage) : m_size(size)
{
    VK_CORE_INFO("Created TransferBuffer");
    auto& instance = VulkanRendererAPI::Get();

    VmaMemoryUsage memoryUsage = {};
        
    switch (usage)
    {
    case VanKTransferBufferUsageUpload: memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU; break;
    case VanKTransferBufferUsageDownload: memoryUsage = VMA_MEMORY_USAGE_GPU_TO_CPU; break;
    }

    m_transferBuffer = instance.GetAllocator().createBuffer
    (
        size,
        vk::BufferUsageFlagBits2::eTransferSrc,
        memoryUsage,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    DBG_VK_NAME(m_transferBuffer.buffer);
}

VanK::VulkanTransferBuffer::~VulkanTransferBuffer()
{
    VK_CORE_INFO("Destroyed TransferBuffer");
    auto& instance = VulkanRendererAPI::Get();
    
    instance.GetAllocator().destroyBuffer(m_transferBuffer);
}

void VanK::VulkanTransferBuffer::Bind() const
{
}

void VanK::VulkanTransferBuffer::Unbind() const
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

void* VanK::VulkanTransferBuffer::MapTransferBuffer(uint64_t size, uint64_t alignment, uint64_t& outOffset)
{
    auto& instance = VulkanRendererAPI::Get();

    // Map and copy data to the staging buffer
    void* mappedPtr = nullptr;
    if (static_cast<vk::Result>(vmaMapMemory(instance.GetAllocator(), m_transferBuffer.allocation, &mappedPtr)) != vk::Result::eSuccess)
    {
        return nullptr;
    }

    //ring buffer offset

    // --- check if request itself is too large ---
    if (size > m_size) {
        VK_CORE_ERROR("VulkanTransferBuffer::MapTransferBuffer Requested transfer size ({0}) exceeds transfer buffer size ({1})!", size, m_size);
        VK_CORE_ASSERT(false, "Transfer size too large!");
    }

    // Align offset
    VkDeviceSize alignedOffset = (m_currentOffset + alignment - 1) & ~(alignment - 1);

    // Check if we have enough space
    if (alignedOffset + size > m_size)
    {
        // wrap around
        alignedOffset = 0;
    }
    
    outOffset = alignedOffset;
    m_currentOffset = alignedOffset + size;

    return static_cast<uint8_t*>(mappedPtr) + alignedOffset;
}

void VanK::VulkanTransferBuffer::UnMapTransferBuffer()
{
    auto& instance = VulkanRendererAPI::Get();
    vmaUnmapMemory(instance.GetAllocator(), m_transferBuffer.allocation);
}

void VanK::VulkanTransferBuffer::UploadToGPUBuffer(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion)
{
    if (bufferRegion.size == 0)
        return; // or skip the copy safely

    if (bufferRegion.buffer == VK_NULL_HANDLE) {
        std::cerr << "Error: bufferRegion.buffer is null!" << std::endl;
        return;
    }
        
    VkBufferCopy copyRegion;
    copyRegion.srcOffset = location.offset;
    copyRegion.dstOffset = bufferRegion.offset;
    copyRegion.size = bufferRegion.size;
    //maybe copyregion array idk

    // Add a barrier to make sure nothing was writing to it, before updating its content
    utils::cmdBufferMemoryBarrier
    (
        Unwrap(cmd),
        static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
        vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
        vk::PipelineStageFlagBits2::eTransfer
    );
        
    vkCmdCopyBuffer(*Unwrap(cmd), m_transferBuffer.buffer, static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()), 1, &copyRegion);
        
    // Add barrier to make sure the buffer is updated before the fragment shader uses it
    utils::cmdBufferMemoryBarrier
    (
        Unwrap(cmd),
        static_cast<VkBuffer>(bufferRegion.buffer->GetNativeHandle()),
        vk::PipelineStageFlagBits2::eTransfer,
        vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader
    );
}

VanK::VulkanUniformBuffer::VulkanUniformBuffer(uint64_t size)
{
    VK_CORE_INFO("Created UniformBuffer");
    auto& instance = VulkanRendererAPI::Get();

    m_uniformBuffer = instance.GetAllocator().createBuffer
    (
        size,
        vk::BufferUsageFlagBits2::eUniformBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    DBG_VK_NAME(m_uniformBuffer.buffer);
}

VanK::VulkanUniformBuffer::~VulkanUniformBuffer()
{
    VK_CORE_INFO("Destroyed UniformBuffer");
    auto& instance = VulkanRendererAPI::Get();
    
    instance.GetAllocator().destroyBuffer(m_uniformBuffer);
}

void VanK::VulkanUniformBuffer::Bind() const
{
}

void VanK::VulkanUniformBuffer::Unbind() const
{
}

void VanK::VulkanUniformBuffer::Update(VanKCommandBuffer cmd, const void* data, size_t size)
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
    vkCmdUpdateBuffer(*Unwrap(cmd), m_uniformBuffer.buffer, 0, size, data);

    // Add a memory barrier after updating
    utils::cmdBufferMemoryBarrier
    (
        Unwrap(cmd),
        m_uniformBuffer.buffer,
        vk::PipelineStageFlagBits2::eTransfer,
        vk::PipelineStageFlagBits2::eFragmentShader
    );
}

VanK::VulkanStorageBuffer::VulkanStorageBuffer(uint64_t size)
{
    VK_CORE_INFO("Created StorageBuffer");
    auto& instance = VulkanRendererAPI::Get();

    m_storageBuffer = instance.GetAllocator().createBuffer
    (
        size,
        vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    DBG_VK_NAME(m_storageBuffer.buffer);
}

VanK::VulkanStorageBuffer::~VulkanStorageBuffer()
{
    VK_CORE_INFO("Destroyed StorageBuffer");
    auto& instance = VulkanRendererAPI::Get();
    
    instance.GetAllocator().destroyBuffer(m_storageBuffer);
}

void VanK::VulkanStorageBuffer::Bind() const
{
}

void VanK::VulkanStorageBuffer::Unbind() const
{
}

void VanK::VulkanStorageBuffer::Upload(const void* data, size_t size, size_t offset)
{
}
