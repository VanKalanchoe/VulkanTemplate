#pragma once
#include <cstdint>
#include <span>
#include <array>
#include <string>
#include <glm/glm.hpp>
#include <vector>

#include "VanK/Core/Ref.h"

namespace VanK
{
    // Forward declarations
    struct VanKCommandBuffer_T;
    using VanKCommandBuffer = VanKCommandBuffer_T*;

    enum class ShaderDataType
    {
        None = 0, Float, Float2, Float3, Float4, Mat3, Mat4, Int, Int2, Int3, Int4, Bool
    };

    static uint32_t ShaderDataTypeSize(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float:  return 4;
        case ShaderDataType::Float2: return 4 * 2;
        case ShaderDataType::Float3: return 4 * 3;
        case ShaderDataType::Float4: return 4 * 4;
        case ShaderDataType::Mat3:   return 4 * 3 * 3;
        case ShaderDataType::Mat4:   return 4 * 4 * 4;
        case ShaderDataType::Int:    return 4;
        case ShaderDataType::Int2:   return 4 * 2;
        case ShaderDataType::Int3:   return 4 * 3;
        case ShaderDataType::Int4:   return 4 * 4;
        case ShaderDataType::Bool:   return 1;
        }

        //core assert hatzel

        return 0;
    }

    struct BufferElement
    {
        std::string Name;
        ShaderDataType Type;
        uint32_t Size;
        uint32_t Offset;
        bool Normalized;

        BufferElement() = default;
    
        BufferElement(ShaderDataType type, const std::string& name, bool normalized = false)
            : Name(name), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized)
        {
        }

        uint32_t GetComponentCount() const
        {
            switch (Type)
            {
            case ShaderDataType::Float:  return 1;
            case ShaderDataType::Float2: return 2;
            case ShaderDataType::Float3: return 3;
            case ShaderDataType::Float4: return 4;
            case ShaderDataType::Mat3:   return 3 * 3;
            case ShaderDataType::Mat4:   return 4 * 4;
            case ShaderDataType::Int:    return 1;
            case ShaderDataType::Int2:   return 2;
            case ShaderDataType::Int3:   return 3;
            case ShaderDataType::Int4:   return 4;
            case ShaderDataType::Bool:   return 1;
            }
            //core assert hatzel
            return 0;
        }
    };

    class BufferLayout
    {
    public:
        BufferLayout() = default;
        BufferLayout(const std::initializer_list<BufferElement>& elements)
            : m_Elements(elements)
        {
            CalculateOffsetsAndStride();
        }

        inline uint32_t GetStride() const { return m_Stride; }
        inline const std::vector<BufferElement>& GetElements() const { return m_Elements; }

        std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
        std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
        std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
        std::vector<BufferElement>::const_iterator end() const { return m_Elements.end(); }
    private:
        void CalculateOffsetsAndStride()
        {
            uint32_t offset = 0;
            m_Stride = 0;
            for (auto& element : m_Elements)
            {
                element.Offset = offset;
                offset += element.Size;
                m_Stride += element.Size;
            }
        }
    private:
        std::vector<BufferElement> m_Elements;
        uint32_t m_Stride = 0;
    };

    class VanKBuffer : public RefCounted
    {
    public:
        virtual ~VanKBuffer() {}

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        virtual uint64_t GetBufferAddress() const = 0;
        virtual void* GetNativeHandle() const = 0;
        virtual uint64_t GetSize() const = 0;

        static VanKBuffer* Create(uint64_t size);
    };

    class VertexBuffer : public VanKBuffer
    {
    public:
        virtual ~VertexBuffer() {}

        virtual void Bind() const override = 0;
        virtual void Unbind() const override = 0;
        virtual uint64_t GetBufferAddress() const override = 0;
        virtual void* GetNativeHandle() const override = 0;
        virtual uint64_t GetSize() const = 0;

        // Upload for initial setup
        virtual void Upload(const void* data, size_t size) = 0;

        static VertexBuffer* Create(uint64_t size);
    };

    class IndexBuffer : public VanKBuffer
    {
    public:
        virtual ~IndexBuffer() {}

        virtual void Bind() const override = 0;
        virtual void Unbind() const override = 0;
        virtual uint64_t GetBufferAddress() const override = 0;
        virtual void* GetNativeHandle() const override = 0;
        virtual uint64_t GetSize() const = 0;

        virtual uint32_t GetCount() const = 0;

        // Upload for initial setup
        virtual void Upload(const void* data, size_t size, size_t offset) = 0;
        
        static IndexBuffer* Create(uint64_t bufferSize);
    };

    enum VanKTransferBufferUsage
    {
        VanKTransferBufferUsageUpload,
        VanKTransferBufferUsageDownload
    };

    struct VanKTransferBufferLocation
    {
        //TransferBuffer* transfer_buffer;  // Pointer to CPU-visible staging buffer
        uint64_t offset;                  // Offset inside that buffer
    };

    struct VanKBufferRegion
    {
        VanKBuffer* buffer;     // Destination GPU buffer (device-local)
        uint64_t offset;        // Offset inside destination buffer
        uint64_t size;          // Size of data to copy
    };

    class TransferBuffer : public VanKBuffer
    {
    public:
        virtual ~TransferBuffer() {}

        virtual void Bind() const override = 0;
        virtual void Unbind() const override = 0;
        virtual uint64_t GetBufferAddress() const override = 0;
        virtual void* GetNativeHandle() const override = 0;
        virtual uint64_t GetSize() const = 0;

        virtual void* MapTransferBuffer(uint64_t size, uint64_t alignment, uint64_t& outOffset) = 0;
        virtual void UnMapTransferBuffer() = 0;

        virtual void UploadToGPUBuffer(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion, bool runtime = true) = 0;
        virtual void DownloadFromGPUBuffer(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion) = 0;
        virtual void UploadRaw(VanKCommandBuffer& cmd, VanKBuffer& dstBuffer, const void* vecData, uint64_t dataSize, uint64_t alignment, uint64_t dstOffset, bool runtime = true) = 0;
        template <class T>
        void Upload(VanKCommandBuffer& cmd, VanKBuffer& dstBuffer, const std::vector<T>& vecData, uint64_t dstOffset, bool runtime = true)
        {
            if (vecData.empty())
                return;

            UploadRaw(cmd, dstBuffer, vecData.data(), vecData.size() * sizeof(T), alignof(T), dstOffset, runtime);
        }
        
        static TransferBuffer* Create(uint64_t size, VanKTransferBufferUsage usage);
    };

    class UniformBuffer : public VanKBuffer
    {
    public:
        virtual ~UniformBuffer() {}

        virtual void Bind() const override = 0;
        virtual void Unbind() const override = 0;
        virtual uint64_t GetBufferAddress() const override = 0;
        virtual void* GetNativeHandle() const override = 0;
        virtual uint64_t GetSize() const = 0;
    
        // Uniform buffers use vkCmdUpdateBuffer (special case)
        virtual void Update(VanKCommandBuffer cmd, const void* data, size_t size) = 0;

        static UniformBuffer* Create(uint64_t size);
    };

    class StorageBuffer : public VanKBuffer
    {
    public:
        virtual ~StorageBuffer() = default;

        virtual void Bind() const override = 0;
        virtual void Unbind() const override = 0;
        virtual uint64_t GetBufferAddress() const override = 0;
        virtual void* GetNativeHandle() const override = 0;
        virtual uint64_t GetSize() const = 0;

        // Upload - for initial setup (creates its own command buffer)
        virtual void Upload(const void* data, size_t size, size_t offset) = 0;
        virtual void Fill(VanKCommandBuffer cmd, uint64_t dstOffset, uint64_t size, uint32_t data) = 0;

        static StorageBuffer* Create(uint64_t size);
    };

    class IndirectBuffer : public VanKBuffer
    {
    public:
        virtual ~IndirectBuffer() = default;

        virtual void Bind() const override = 0;
        virtual void Unbind() const override = 0;
        virtual uint64_t GetBufferAddress() const override = 0;
        virtual void* GetNativeHandle() const override = 0;
        virtual uint64_t GetSize() const = 0;

        // Upload - for initial setup (creates its own command buffer)
        virtual void Upload(const void* data, size_t size, size_t offset) = 0;
        virtual void Fill(VanKCommandBuffer cmd, uint64_t dstOffset, uint64_t size, uint32_t data) = 0;

        static IndirectBuffer* Create(uint64_t size);
    };
    
    struct BufferHandle 
    {
        uint32_t id = 0;

        bool operator==(const BufferHandle& other) const { return id == other.id; }
        bool valid() const { return id != 0; }
    };
    
    class BufferManager
    {
    public:
        template<typename T, typename... Args>
        BufferHandle Create(Args&&... args)
        {
            uint32_t newID = ++lastID;

            if constexpr (std::is_same_v<T, StorageBuffer>)
            {
                StorageBuffer* buffer =
                    StorageBuffer::Create(std::forward<Args>(args)...);

                m_storageBuffers[newID] = Ref<StorageBuffer>(buffer);
            }
            else if constexpr (std::is_same_v<T, TransferBuffer>)
            {
                TransferBuffer* buffer =
                    TransferBuffer::Create(std::forward<Args>(args)...);

                m_transferBuffers[newID] = Ref<TransferBuffer>(buffer);
            }
            else if constexpr (std::is_same_v<T, IndirectBuffer>)
            {
                IndirectBuffer* buffer =
                    IndirectBuffer::Create(std::forward<Args>(args)...);

                m_indirectBuffers[newID] = Ref<IndirectBuffer>(buffer);
            }
            else
            {
                static_assert(sizeof(T) == 0, "Unsupported buffer type");
            }

            return { newID };
        }

        template<typename T>
        Ref<T> Get(BufferHandle handle)
        {
            if constexpr (std::is_same_v<T, StorageBuffer>)
            {
                auto it = m_storageBuffers.find(handle.id);
                if (it != m_storageBuffers.end())
                    return it->second;
            }
            else if constexpr (std::is_same_v<T, TransferBuffer>)
            {
                auto it = m_transferBuffers.find(handle.id);
                if (it != m_transferBuffers.end())
                    return it->second;
            }
            else if constexpr (std::is_same_v<T, IndirectBuffer>)
            {
                auto it = m_indirectBuffers.find(handle.id);
                if (it != m_indirectBuffers.end())
                    return it->second;
            }
            else
            {
                static_assert(sizeof(T) == 0, "Unsupported buffer type");
            }

            return {};
        }

        void Shutdown()
        {
            m_storageBuffers.clear();
            m_transferBuffers.clear();
            m_indirectBuffers.clear();
        }

    private:
        std::unordered_map<uint32_t, Ref<StorageBuffer>> m_storageBuffers;
        std::unordered_map<uint32_t, Ref<TransferBuffer>> m_transferBuffers;
        std::unordered_map<uint32_t, Ref<IndirectBuffer>> m_indirectBuffers;
        uint32_t lastID = 0;
    };
}
