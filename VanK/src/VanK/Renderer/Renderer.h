#pragma once
#include "RenderCommand.h"
#include "VanK/Core/Window.h"
#include "FileWatch.h"
#include "VanK/Core/core.h"

namespace VanK
{struct Vertex
{
    glm::vec3 pos;
    glm::vec4 color;
    glm::vec2 texCoord;
};
    class Renderer
    {
        #define UPLOAD_ARRAY_TO_RING_BUFFER(cmd, ringBuffer, targetBuffer, array, ElementType) \
            do { \
                uint64_t offset; \
                const size_t dataSize = sizeof(array); \
                ElementType* dataPtr = static_cast<ElementType*>(ringBuffer->MapTransferBuffer(dataSize, alignof(ElementType), offset)); \
                memcpy(dataPtr, array, dataSize); \
                ringBuffer->UnMapTransferBuffer(); \
                ringBuffer->UploadToGPUBuffer(cmd, VanKTransferBufferLocation{.offset = offset}, \
                VanKBufferRegion{.buffer = targetBuffer.get(), .offset = 0, .size = dataSize}); \
            } while(0)

        #define UploadBufferToGpuWithTransferRing(cmd, ringBuffer, targetBuffer, vector, ElementType, dstOffset) \
            do { \
                if (!vector.empty()) { \
                    uint64_t offset; \
                    const size_t dataSize = vector.size() * sizeof(ElementType); \
                    ElementType* dataPtr = static_cast<ElementType*>(ringBuffer->MapTransferBuffer(dataSize, alignof(ElementType), offset)); \
                    memcpy(dataPtr, vector.data(), dataSize); \
                    ringBuffer->UnMapTransferBuffer(); \
                    ringBuffer->UploadToGPUBuffer(cmd, VanKTransferBufferLocation{.offset = offset}, \
                    VanKBufferRegion{.buffer = targetBuffer.get(), .offset = dstOffset, .size = dataSize}); \
                } \
            } while(0)
        
    public:
        static void loadModel();
        static void Init(Window& window);
        static void Shutdown();
        static void BeginSubmit();
        static void EndSubmit();
        static void DrawFrame();
        static void Flush();
    private:
        static ShaderLibrary& GetShaderLibrary() { return m_ShaderLibrary; }
        static void RegisterPipelineForShaderWatcher(const std::string& shaderKey, const std::string& fileName, VanKGraphicsPipelineSpecification* graphicsSpec, VanKComputePipelineSpecification* computeSpec,
                                                     VanKPipeLine* pipeline, VanKShaderStageFlags flag);
        static void WatchShaderFiles();
        static void ReloadPipelines();
    private:
        inline static std::vector<Vertex> vertices;
        inline static std::vector<uint32_t> indices;
        inline static bool vSync = false;
        inline static bool windowMinimized = false;
        inline static Extent2D m_ViewportSize;
        inline static Extent2D lastViewportExtent = {0, 0};
        inline static VanKCommandBuffer cmd = nullptr;
        inline static ShaderLibrary m_ShaderLibrary;
        inline static VanKPipeLine m_GraphicsDebugPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsDebugPipelineSpecification = {};
        inline static VanKPipeLine m_ComputeDrawIndirectPipeline = {};
        inline static VanKComputePipelineSpecification m_ComputeDrawIndirectPipelineSpecification = {};
        inline static Ref<UniformBuffer> uniformScene;
        inline static Ref<TransferBuffer> transferRing;
        inline static Ref<IndirectBuffer> indirectBuffer;
        inline static Ref<IndirectBuffer> countBuffer;
        inline static Ref<VertexBuffer> vertexMesh;
        inline static Ref<IndexBuffer> indexMesh;
    };
}
