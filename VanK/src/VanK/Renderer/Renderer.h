#pragma once
#include "EditorCamera.h"
#include "RenderCommand.h"
#include "VanK/Core/Window.h"
#include "FileWatch.h"
#include "VanK/Core/core.h"

#include "Geometry.h"
#include "VanK/Asset/TextureImporter.h"
#include "VanK/Scene/Components.h"

namespace VanK
{
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
        inline static SDL_Window* m_window = nullptr; //remove from here
        static void loadModel();
        static void BeginScene(const EditorCamera& camera);
        static void BeginScene(const Camera& camera, const glm::mat4& transform);
        static void EndScene();
        
        static void DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);
        static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f), int entityID = -1);
        static void DrawSprite(const glm::mat4& transform, SpriteRendererComponent& src, int entityID);
        static void DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness = 1.0f, float fade = 0.005f, int entityID = -1);
        struct TextParams
        {
            glm::vec4 Color{ 1.0f };
            float Kerning = 0.0f;
            float LineSpacing = 0.0f;
        };
        static void DrawString(const std::string& string, Ref<Font> font, const glm::mat4& transform, const TextParams& textParams, int entityID = -1);
        static void DrawString(const std::string& string, const glm::mat4& transform, const TextComponent& component, int entityID = -1);
        
        static void Init(Window& window);
        static void Shutdown();
        static void CheckPendingVSyncChange();
        static void BeginSubmit();
        static void EndSubmit();
        static void Flush();
        static void DrawFrame();
        static bool GetVSync() { return vSync; };
        static void QueVSyncChange(bool vSyncTemp) { vSync = vSyncTemp; s_VSyncChangeRequested = true; };
        static bool GetIsEditor() { return isEditor; }
        static void SetViewportSize(Extent2D viewportSize) { m_ViewportSize = viewportSize; RenderCommand::setViewportSize(viewportSize); }
        static void SetWindowMinimized(bool minimized) { windowMinimized = minimized; }
        static bool isWindowMinimized() { return windowMinimized; }
        
    private:
        static ShaderLibrary& GetShaderLibrary() { return m_ShaderLibrary; }
        static void RegisterPipelineForShaderWatcher(const std::string& shaderKey, const std::string& fileName, VanKGraphicsPipelineSpecification* graphicsSpec, VanKComputePipelineSpecification* computeSpec,
                                                     VanKPipeLine* pipeline, VanKShaderStageFlags flag);
        static void WatchShaderFiles();
        static void ReloadPipelines();
        
    private:
        inline static bool isEditor = true; //remove from here
        inline static std::vector<shaderio::InstancedVertexData> vertices;
        inline static std::vector<uint32_t> indices;
        inline static bool vSync = false;
        inline static bool s_VSyncChangeRequested = false;
        inline static bool windowMinimized = false;
        inline static Extent2D m_ViewportSize  = {640, 480}; // selber gemacht muss mit editorlayer verknüpft werden
        inline static Extent2D lastViewportExtent = {0, 0};
        inline static VanKCommandBuffer cmd = nullptr;
        inline static ShaderLibrary m_ShaderLibrary;
        
        // Graphics Pipelines
        inline static VanKPipeLine m_GraphicsDebugPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsDebugPipelineSpecification = {};
        
        inline static VanKPipeLine m_GraphicsCirclePipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsCirclePipelineSpecification = {};
        
        inline static VanKPipeLine m_GraphicsTextPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsTextPipelineSpecification = {};
        
        inline static VanKPipeLine m_GraphicsLinePipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsLinePipelineSpecification = {};
        
        // Compute Pipelines
        inline static VanKPipeLine m_ComputeDrawIndirectPipeline = {};
        inline static VanKComputePipelineSpecification m_ComputeDrawIndirectPipelineSpecification = {};
        
        inline static Ref<UniformBuffer> uniformScene;
        
        inline static Ref<IndexBuffer> m_InstancedIndexBuffer;
        
        inline static Ref<VertexBuffer> m_InstancedVertexBuffer; // change to storage in the future maybe ? 
        
        inline static Ref<TransferBuffer> m_TransferRingBuffer;
        
        inline static Ref<StorageBuffer> m_InstancedStorageBuffer;
        inline static Ref<StorageBuffer> m_InstancedCircleBuffer;
        inline static Ref<StorageBuffer> m_InstancedTextBuffer;
        inline static Ref<StorageBuffer> m_MeshInfoBuffer;
        
        inline static Ref<IndirectBuffer> m_IndirectBuffer;
        inline static Ref<IndirectBuffer> m_CountBuffer;
        
        inline static Ref<Texture2D> whiteTexture, vikingRoom, ChernoLogo;
    };
}
