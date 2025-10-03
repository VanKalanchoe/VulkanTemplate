#include "Buffer.h"
#include "RendererAPI.h"
#include "Platform/Vulkan/VulkanBuffer.h"

namespace VanK
{
    VanKBuffer* VanKBuffer::Create(uint64_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanVanKBuffer(size);
        }
        return nullptr;
    }

    VertexBuffer* VertexBuffer::Create(uint64_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanVertexBuffer(size);
        }
        return nullptr;
    }

    IndexBuffer* IndexBuffer::Create(uint64_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanIndexBuffer(size);
        }
        return nullptr;
    }

    TransferBuffer* TransferBuffer::Create(uint64_t size, VanKTransferBufferUsage usage)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanTransferBuffer(size, usage);
        }
        return nullptr;
    }

    UniformBuffer* UniformBuffer::Create(uint64_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanUniformBuffer(size);
        }
        return nullptr;
    }

    StorageBuffer* StorageBuffer::Create(uint64_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanStorageBuffer(size);
        }
        return nullptr;
    }

    IndirectBuffer* IndirectBuffer::Create(uint64_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanIndirectBuffer(size);
        }
        return nullptr;
    }
}