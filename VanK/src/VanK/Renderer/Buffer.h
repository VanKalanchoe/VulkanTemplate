#pragma once
#include <cstdint>
#include <span>
#include <array>
#include <string>
#include <glm/glm.hpp>
#include <vector>

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

    class VanKBuffer
    {
    public:
        virtual ~VanKBuffer() {}

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        virtual uint64_t GetBufferAddress() const = 0;
        virtual void* GetNativeHandle() const = 0;

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

        virtual void* MapTransferBuffer(uint64_t size, uint64_t alignment, uint64_t& outOffset) = 0;
        virtual void UnMapTransferBuffer() = 0;

        virtual void UploadToGPUBuffer(VanKCommandBuffer cmd, VanKTransferBufferLocation location, VanKBufferRegion bufferRegion) = 0;

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

        // Upload - for initial setup (creates its own command buffer)
        virtual void Upload(const void* data, size_t size, size_t offset) = 0;

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

        // Upload - for initial setup (creates its own command buffer)
        virtual void Upload(const void* data, size_t size, size_t offset) = 0;

        static IndirectBuffer* Create(uint64_t size);
    };
}