#include "Buffer.h"
#include "RendererAPI.h"
#include "Platform/Vulkan/VulkanBuffer.h"

namespace VanK
{
    VanKBuffer* VanKBuffer::Create(uint64_t bufferSize)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanVanKBuffer(bufferSize);
        }
        return nullptr;
    }

    VertexBuffer* VertexBuffer::Create(uint64_t bufferSize)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanVertexBuffer(bufferSize);
        }
        return nullptr;
    }

    IndexBuffer* IndexBuffer::Create(uint64_t bufferSize)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanIndexBuffer(bufferSize);
        }
        return nullptr;
    }

    TransferBuffer* TransferBuffer::Create(uint64_t bufferSize, VanKTransferBufferUsage usage)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanTransferBuffer(bufferSize, usage);
        }
        return nullptr;
    }

    UniformBuffer* UniformBuffer::Create(uint64_t bufferSize)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanUniformBuffer(bufferSize);
        }
        return nullptr;
    }

    StorageBuffer* StorageBuffer::Create(uint64_t bufferSize)
    {
        switch (RendererAPI::GetAPI())
        {
        case RenderAPIType::None: return nullptr;
        case RenderAPIType::Vulkan: return new VulkanStorageBuffer(bufferSize);
        }
        return nullptr;
    }
}