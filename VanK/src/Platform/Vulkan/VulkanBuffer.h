#pragma once
#include <deque>

#include "VanK/Renderer/Buffer.h"
#include "VulkanRendererAPI.h"
namespace VanK
{
    class VulkanVanKBuffer : public VanKBuffer
    {
    public:
        VulkanVanKBuffer(uint64_t bufferSize);
        virtual ~VulkanVanKBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_buffer.address; }
        virtual void* GetNativeHandle() const override { return (void*)m_buffer.buffer; }
    
        const utils::Buffer& GetBuffer() const { return m_buffer; }

    private:
        utils::Buffer m_buffer;
    };

    class VulkanVertexBuffer : public VertexBuffer
    {
    public:
        VulkanVertexBuffer(uint64_t bufferSize);
        virtual ~VulkanVertexBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_vertexBuffer.address; }
        virtual void* GetNativeHandle() const override { return (void*)m_vertexBuffer.buffer; }
    
        // Upload for initial setup
        virtual void Upload(const void* data, size_t size) override;
        
        const utils::Buffer& GetBuffer() const { return m_vertexBuffer; }

    private:
        uint32_t m_RendererID;
        utils::Buffer m_vertexBuffer;
    };

    class VulkanIndexBuffer : public IndexBuffer
    {
    public:
        VulkanIndexBuffer(uint64_t bufferSize);
        virtual ~VulkanIndexBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_indexBuffer.address; }
        virtual void* GetNativeHandle() const override { return (void*)m_indexBuffer.buffer; }

        uint32_t GetCount() const override { return m_Count; }
    
        // Upload for initial setup
        virtual void Upload(const void* data, size_t size, size_t offset) override;
    
        const utils::Buffer& GetBuffer() const { return m_indexBuffer; }

    private:
        uint32_t m_RendererID;
        uint32_t m_Count;
        utils::Buffer m_indexBuffer;
    };

    class VulkanTransferBuffer : public TransferBuffer
    {
    public:
        VulkanTransferBuffer(uint64_t size, VanKTransferBufferUsage usage);
        virtual ~VulkanTransferBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_transferBuffer.address; }
        virtual void* GetNativeHandle() const override { return (void*)m_transferBuffer.buffer; }
    
        const utils::Buffer& GetBuffer() const { return m_transferBuffer; }

        virtual void* MapTransferBuffer(uint64_t size, uint64_t alignment, uint64_t& outOffset) override;
        virtual void UnMapTransferBuffer() override;
        virtual void UploadToGPUBuffer(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion) override;

    private:
        uint32_t m_RendererID;
        utils::Buffer m_transferBuffer;
        VkDeviceSize m_currentOffset = 0;
        VkDeviceSize m_size = 0;
    };

    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer(uint64_t size);
        virtual ~VulkanUniformBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_uniformBuffer.address; }
        virtual void* GetNativeHandle() const override { return (void*)m_uniformBuffer.buffer; }

        // Uniform buffers use vkCmdUpdateBuffer with memory barriers
        virtual void Update(VanKCommandBuffer cmd, const void* data, size_t size) override;
    
        const utils::Buffer& GetBuffer() const { return m_uniformBuffer; }

    private:
        uint32_t m_RendererID;
        utils::Buffer m_uniformBuffer;
    };

    class VulkanStorageBuffer : public StorageBuffer
    {
    public:
        VulkanStorageBuffer(uint64_t size);
        virtual ~VulkanStorageBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_storageBuffer.address; }
        virtual void* GetNativeHandle() const override { return (void*)m_storageBuffer.buffer; }

        // Upload for initial setup
        virtual void Upload(const void* data, size_t size, size_t offset) override;

        const utils::Buffer& GetBuffer() const { return m_storageBuffer; }

    private:
        utils::Buffer m_storageBuffer;
    };
}