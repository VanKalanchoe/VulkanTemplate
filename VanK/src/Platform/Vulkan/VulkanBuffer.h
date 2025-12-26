#pragma once
#include "VanK/Renderer/Buffer.h"

#include "VulkanRendererAPI.h"

namespace VanK
{
    class VulkanVanKBuffer : public VanKBuffer
    {
    public:
        VulkanVanKBuffer(uint64_t size);
        virtual ~VulkanVanKBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_buffer.address; }
        virtual void* GetNativeHandle() const override { return static_cast<vk::Buffer>(m_buffer.buffer); } // not sure
        virtual uint64_t GetSize() const override { return m_size; }
        
        const utils::Buffer& GetBuffer() const { return m_buffer; }

    private:
        utils::Buffer m_buffer;
        uint64_t m_size = 0;
    };

    class VulkanVertexBuffer : public VertexBuffer
    {
    public:
        VulkanVertexBuffer(uint64_t size);
        virtual ~VulkanVertexBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_vertexBuffer.address; }
        virtual void* GetNativeHandle() const override { return static_cast<vk::Buffer>(m_vertexBuffer.buffer); }
        virtual uint64_t GetSize() const override { return m_size; }
        
        // Upload for initial setup
        virtual void Upload(const void* data, size_t size) override;
        
        const utils::Buffer& GetBuffer() const { return m_vertexBuffer; }

    private:
        utils::Buffer m_vertexBuffer;
        uint64_t m_size = 0;
    };

    class VulkanIndexBuffer : public IndexBuffer
    {
    public:
        VulkanIndexBuffer(uint64_t size);
        virtual ~VulkanIndexBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_indexBuffer.address; }
        virtual void* GetNativeHandle() const override { return static_cast<vk::Buffer>(m_indexBuffer.buffer); }
        virtual uint64_t GetSize() const override { return m_size; }
        
        uint32_t GetCount() const override { return m_Count; }
    
        // Upload for initial setup
        virtual void Upload(const void* data, size_t size, size_t offset) override;
    
        const utils::Buffer& GetBuffer() const { return m_indexBuffer; }

    private:
        uint32_t m_Count;
        utils::Buffer m_indexBuffer;
        uint64_t m_size = 0;
    };

    class VulkanTransferBuffer : public TransferBuffer
    {
    public:
        VulkanTransferBuffer(uint64_t size, VanKTransferBufferUsage usage);
        virtual ~VulkanTransferBuffer();
        virtual uint64_t GetSize() const override { return m_size; }
        
        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_transferBuffer.address; }
        virtual void* GetNativeHandle() const override { return static_cast<vk::Buffer>(m_transferBuffer.buffer); }
    
        const utils::Buffer& GetBuffer() const { return m_transferBuffer; }

        virtual void* MapTransferBuffer(uint64_t size, uint64_t alignment, uint64_t& outOffset) override;
        virtual void UnMapTransferBuffer() override;
        virtual void UploadToGPUBuffer(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion) override;
        virtual void DownloadFromGPUBuffer(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion) override;
    private:
        utils::Buffer m_transferBuffer;
        VkDeviceSize m_currentOffset = 0;
        uint64_t m_size = 0;
    };

    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer(uint64_t size);
        virtual ~VulkanUniformBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_uniformBuffer.address; }
        virtual void* GetNativeHandle() const override { return static_cast<vk::Buffer>(m_uniformBuffer.buffer); }
        virtual uint64_t GetSize() const override { return m_size; }
        
        // Uniform buffers use vkCmdUpdateBuffer with memory barriers
        virtual void Update(VanKCommandBuffer cmd, const void* data, size_t size) override;
    
        const utils::Buffer& GetBuffer() const { return m_uniformBuffer; }

    private:
        utils::Buffer m_uniformBuffer;
        uint64_t m_size = 0;
    };

    class VulkanStorageBuffer : public StorageBuffer
    {
    public:
        VulkanStorageBuffer(uint64_t size);
        virtual ~VulkanStorageBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_storageBuffer.address; }
        virtual void* GetNativeHandle() const override { return static_cast<vk::Buffer>(m_storageBuffer.buffer); }
        virtual uint64_t GetSize() const override { return m_size; }
        
        // Upload for initial setup
        virtual void Upload(const void* data, size_t size, size_t offset) override;
        virtual void Fill(VanKCommandBuffer cmd, uint64_t dstOffset, uint64_t size, uint32_t data) override;

        const utils::Buffer& GetBuffer() const { return m_storageBuffer; }

    private:
        utils::Buffer m_storageBuffer;
        uint64_t m_size = 0;
    };

    class VulkanIndirectBuffer : public IndirectBuffer
    {
    public:
        VulkanIndirectBuffer(uint64_t size);
        virtual ~VulkanIndirectBuffer();

        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint64_t GetBufferAddress() const override { return m_indirectBuffer.address; }
        virtual void* GetNativeHandle() const override { return static_cast<vk::Buffer>(m_indirectBuffer.buffer); }
        virtual uint64_t GetSize() const override { return m_size; }
        
        // Upload for initial setup
        virtual void Upload(const void* data, size_t size, size_t offset) override;
        virtual void Fill(VanKCommandBuffer cmd, uint64_t dstOffset, uint64_t size, uint32_t data) override;

        const utils::Buffer& GetBuffer() const { return m_indirectBuffer; }

    private:
        utils::Buffer m_indirectBuffer;
        uint64_t m_size = 0;
    };
}