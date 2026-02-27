#pragma once
#include "EditorCamera.h"
#include "RenderCommand.h"
#include "VanK/Core/Window.h"
#include "FileWatch.h"
#include "VanK/Core/core.h"

#include "VanK/Asset/TextureImporter.h"
#include "VanK/Scene/Components.h"

#include "VanK/Renderer/RenderGraph.h"

namespace VanK
{
    class Renderer
    {
    public:
        inline static SDL_Window* m_window = nullptr; //remove from here
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
        static bool GetVSync() { return vSync; };
        static void QueVSyncChange(bool vSyncTemp) { vSync = vSyncTemp; s_VSyncChangeRequested = true; };
        static bool GetIsEditor() { return isEditor; }
        static void SetViewportSize(Extent2D viewportSize) { m_ViewportSize = viewportSize; RenderCommand::setViewportSize(viewportSize); CreateRenderTargets(); resetFrame();}
        static Extent2D GetViewportSize() { return m_ViewportSize; }
        static void SetWindowMinimized(bool minimized) { windowMinimized = minimized; }
        static bool isWindowMinimized() { return windowMinimized; }
        static void SetLineWidth(float lineWidth) { m_LineWidth = lineWidth; }
        static float GetLineWidth() { return m_LineWidth; }
        static void SetCullMode(VanKCullModeFlags flag) { cullMode = flag; }
        static VanKCullModeFlags GetCullMode() { return cullMode; }
        static void SetFrustumCullEnabled(bool frustum) { FrustumCullEnabled = frustum; }
        static bool isFrustumCullEnabled() { return FrustumCullEnabled; }
        static Ref<Texture2D>& getWhiteTexture() { return whiteTexture; };
        static Ref<Texture2D>& getPinkTexture() { return pinkTexture; };
        inline static VanKTimestampPass computeCommandTask;
        inline static VanKTimestampPass renderPassMesh;
        static void CreateRenderTargets();
        static RenderGraph GetRenderGraph() { return renderGraph; };
        
        struct MousePickRequest 
        {
            int x = -1, y = -1;
            bool active = false;
        };
        
        static int32_t getLastPickedID() { return m_LastPickedID; }
        static void setLastPickedID(int32_t id) { m_LastPickedID = id; }
        static MousePickRequest setPickRequest(int32_t x, int32_t y, bool active) { return m_PendingPick = { x, y, active }; }
        static int32_t ReadPixel(uint32_t x, uint32_t y) { return m_BufferManager->Get<TransferBuffer>(m_TransferDownlaoadBuffer)->ReadPixel(x, y); };
        
        // path trace // idk namings whats good
        static void setMaxFrames(uint32_t temp) { s_maxAccumulationFrames = temp; };
        static uint32_t getMaxFrames() { return s_maxAccumulationFrames; };
        static uint32_t getCurrentFrame() { return s_frameIndex; };
        // Reset the frame counter to restart progressive rendering
        static void resetFrame() { s_frameIndex = -1; }
    private:
        static ShaderLibrary& GetShaderLibrary() { return m_ShaderLibrary; }
        static void RegisterPipelineForShaderWatcher(const std::string& shaderKey, const std::string& fileName, VanKGraphicsPipelineSpecification* graphicsSpec, VanKComputePipelineSpecification* computeSpec,
                                                     VanKRaytracingPipelineSpecification* raytracingSpec, VanKPipeLine* pipeline, VanKShaderStageFlags flag);
        static void WatchShaderFiles();
        static void ReloadPipelines();
    
    private:
        inline static RenderGraph renderGraph;
        inline static bool isEditor = true; //remove from here
        inline static bool vSync = false;
        inline static bool s_VSyncChangeRequested = false;
        inline static bool windowMinimized = false;
        inline static Extent2D m_ViewportSize  = {640, 480}; // selber gemacht muss mit editorlayer verknüpft werden
        inline static Extent2D lastViewportExtent = {0, 0};
        inline static float m_LineWidth = 1.0f;
        inline static VanKCullModeFlags cullMode = VanK_CULL_MODE_BACK_BIT;
        inline static VanKCommandBuffer cmd = nullptr;
        inline static ShaderLibrary m_ShaderLibrary;
        inline static std::unique_ptr<BufferManager> m_BufferManager;
        inline static MousePickRequest m_PendingPick;
        inline static int32_t m_LastPickedID = -1;
        
        inline static Ref<RenderTargetImage> sceneImage; // resolve 
        inline static Ref<RenderTargetImage> colorImage; // msaa
        inline static Ref<RenderTargetImage> depthImage; // depth
        inline static Ref<RenderTargetImage> entityImage; // resolve 
        inline static Ref<RenderTargetImage> entityColorImage; // msaa
        inline static Ref<RenderTargetImage> rayTracingImage; // rayTracing Storage Image
        inline static Ref<RenderTargetImage> finalImage; // combines raytracing and sceneImage
        
        inline static VanKSamplerInfo skyboxSampler;
        
        // Graphics Pipelines
        inline static VanKPipeLine m_FinalRenderPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_FinalRenderPipelineSpecification = {};
        
        inline static VanKPipeLine m_MeshPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_MeshPipelineSpecification = {};
        
        inline static VanKPipeLine m_MeshQuadPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_MeshQuadPipelineSpecification = {};
        
        inline static VanKPipeLine m_MeshCirclePipeline = {};
        inline static VanKGraphicsPipelineSpecification m_MeshCirclePipelineSpecification = {};
        
        inline static VanKPipeLine m_MeshTextPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_MeshTextPipelineSpecification = {};
        
        inline static VanKPipeLine m_MeshLinePipeline = {};
        inline static VanKGraphicsPipelineSpecification m_MeshLinePipelineSpecification = {};
        
        inline static VanKPipeLine m_MeshSkyBoxPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_MeshSkyBoxPipelineSpecification = {};
        
        // Compute Pipelines
        inline static VanKPipeLine m_ComputeDrawMeshTaskCommandPipeline = {};
        inline static VanKComputePipelineSpecification m_ComputeDrawMeshTaskCommandPipelineSpecification = {};
        
        // Raytracing Pipelines
        inline static VanKPipeLine m_RaytracingPipeline = {};
        inline static VanKRaytracingPipelineSpecification m_RaytracingPipelineSpecification = {};
        
        inline static VanKPipeLine m_RaytracingPipelines = {};
        inline static VanKRaytracingPipelineSpecification m_RaytracingPipelineSpecifications = {};
    
    public:
        inline static bool isRaster = false;
        inline static bool frozen = false;
        inline static bool frozenDone = false;
        inline static bool FrustumCullEnabled = true;
    private:
        inline static BufferHandle m_TransferBuffer;
        inline static BufferHandle sceneBuffer ;
        inline static BufferHandle cullBuffer ;
        inline static BufferHandle m_TransferDownlaoadBuffer;
        inline static BufferHandle vertexBuffer ;
        inline static BufferHandle indexBuffer ;
        inline static BufferHandle meshletVerticesBuffer ;
        inline static BufferHandle meshletTrianglesBuffer ;
        inline static BufferHandle meshletBuffer ;
        inline static BufferHandle meshletPrimitiveBuffer ;
        inline static BufferHandle meshDrawBuffer ;
        inline static BufferHandle materialBuffer;
        inline static BufferHandle instanceLutsBuffer;
        
        inline static BufferHandle lightsBuffer;
        
        //2d quads, circle, text, line dont need meshlets 1 thread means 1 instance of it
        inline static BufferHandle quadBuffer ;
        inline static BufferHandle circleBuffer ;
        inline static BufferHandle textBuffer ;
        inline static BufferHandle lineBuffer ;
        //--
        
        inline static BufferHandle localMeshTaskSubmitBuffer;
        inline static BufferHandle meshTaskSubmitBuffer;
        
        inline static Ref<Texture2D> whiteTexture, pinkTexture, vikingRoom, ChernoLogo, cubemap, BRDF2DLUT, irradianceMap, prefilterMap, rustedIron, rustedIronMetalRough, rustedIronNormal;
    
        // path trace
        inline static uint32_t s_frameIndex = 0;
        inline static uint32_t s_maxAccumulationFrames = 200;
    };
}
