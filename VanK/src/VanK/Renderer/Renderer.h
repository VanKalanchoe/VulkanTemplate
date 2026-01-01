#pragma once
#include "EditorCamera.h"
#include "RenderCommand.h"
#include "VanK/Core/Window.h"
#include "FileWatch.h"
#include "VanK/Core/core.h"

#include "RegistryMesh.h"
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
        static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID = -1);
        static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID = -1);
        static void DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);
        
        static void Init(Window& window);
        static void Shutdown();
        static void CheckPendingVSyncChange();
        static void BeginSubmit();
        static void EndSubmit();
        static void Flush();
        static void DrawMeshShader();
        static void DrawFrame();
        static bool GetVSync() { return vSync; };
        static void QueVSyncChange(bool vSyncTemp) { vSync = vSyncTemp; s_VSyncChangeRequested = true; };
        static bool GetIsEditor() { return isEditor; }
        static void SetViewportSize(Extent2D viewportSize) { m_ViewportSize = viewportSize; RenderCommand::setViewportSize(viewportSize); }
        static void SetWindowMinimized(bool minimized) { windowMinimized = minimized; }
        static bool isWindowMinimized() { return windowMinimized; }
        static void SetLineWidth(float lineWidth) { m_LineWidth = lineWidth; }
        static float GetLineWidth() { return m_LineWidth; }
        static void SetCullMode(VanKCullModeFlags flag) { cullMode = flag; }
        static VanKCullModeFlags GetCullMode() { return cullMode; }
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
        inline static float m_LineWidth = 1.0f;
        inline static VanKCullModeFlags cullMode = VanK_CULL_MODE_BACK_BIT;
        inline static VanKCommandBuffer cmd = nullptr;
        inline static ShaderLibrary m_ShaderLibrary;
        
        inline static std::array<VanKPipeLine, shaderio::PipelineType_Count> pipelines;
        
        // Graphics Pipelines
        inline static VanKPipeLine m_GraphicsPBRPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsPBRPipelineSpecification = {};
        
        inline static VanKPipeLine m_GraphicsQuadPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsQuadPipelineSpecification = {};
        
        inline static VanKPipeLine m_GraphicsCirclePipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsCirclePipelineSpecification = {};
        
        inline static VanKPipeLine m_GraphicsTextPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsTextPipelineSpecification = {};
        
        inline static VanKPipeLine m_GraphicsLinePipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsLinePipelineSpecification = {};
        
        inline static VanKPipeLine m_MeshPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_MeshPipelineSpecification = {};
        
        inline static VanKPipeLine m_MeshQuadPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_MeshQuadPipelineSpecification = {};
        
        // Compute Pipelines
        inline static VanKPipeLine m_ComputeDrawIndirectPipeline = {};
        inline static VanKComputePipelineSpecification m_ComputeDrawIndirectPipelineSpecification = {};
        
        inline static VanKPipeLine m_ComputeDrawMeshTaskCommandPipeline = {};
        inline static VanKComputePipelineSpecification m_ComputeDrawMeshTaskCommandPipelineSpecification = {};
        
        inline static Ref<UniformBuffer> uniformScene;
        
        inline static Ref<IndexBuffer> m_InstancedIndexBuffer;
        
        inline static Ref<VertexBuffer> m_InstancedVertexBuffer; // change to storage in the future maybe ? 
        
        inline static Ref<TransferBuffer> m_TransferRingBuffer;
        
        inline static Ref<StorageBuffer> m_InstancedPBRBuffer;
        inline static Ref<StorageBuffer> m_InstancedQuadBuffer;
        inline static Ref<StorageBuffer> m_InstancedCircleBuffer;
        inline static Ref<StorageBuffer> m_InstancedTextBuffer;
        inline static Ref<StorageBuffer> m_InstancedLineBuffer;
        inline static Ref<StorageBuffer> m_MeshInfoBuffer;
        
        inline static std::array<Ref<IndirectBuffer>, 5> m_IndirectBuffers;

        inline static std::array<Ref<IndirectBuffer>, 5> m_CountBuffers;
        
    public:
        inline static bool frozen = false;
        inline static bool frozenDone = false;
    private:
        inline static Ref<TransferBuffer> m_TransferBuffer;
        inline static Ref<StorageBuffer> sceneBuffer ;
        inline static Ref<StorageBuffer> cullBuffer ;
        inline static Ref<TransferBuffer> m_TransferDownlaoadBuffer;
        inline static Ref<StorageBuffer> vertexBuffer ;
        inline static Ref<StorageBuffer> meshletVerticesBuffer ;
        inline static Ref<StorageBuffer> meshletTrianglesBuffer ;
        inline static Ref<StorageBuffer> meshletBuffer ;
        inline static Ref<StorageBuffer> meshletPrimitiveBuffer ;
        inline static Ref<StorageBuffer> meshDrawBuffer ;
        
        //2d quads, circle, text, line dont need meshlets 1 thread means 1 instance of it
        inline static Ref<StorageBuffer> quadBuffer ;
        //--
        
        inline static Ref<StorageBuffer> localMeshTaskSubmitBuffer;
        inline static Ref<IndirectBuffer> meshTaskSubmitBuffer;
        
        inline static Ref<Texture2D> whiteTexture, vikingRoom, ChernoLogo;
    };
}
