#pragma once
#include <memory>
#include <optional>
#include <vector>
#include <SDL3/SDL_video.h>

#include "Shader.h"
#include "VanK/Renderer/Buffer.h"
#include <imgui.h>

namespace VanK
{
    // Forward declare an opaque struct (incomplete type)
    struct VanKCommandBuffer_T;
    using VanKCommandBuffer = VanKCommandBuffer_T*;
    
    struct VanKPipeLine_T;
    using VanKPipeLine = VanKPipeLine_T*;

    struct VanKSpecializationMapEntries
    {
        uint32_t constantID;
        uint32_t offset;
        size_t size;
    };

    struct VanKSpecializationInfo
    {
        std::vector<VanKSpecializationMapEntries> MapEntries;     // Owns the entries
        std::vector<uint8_t> Data;                                // Owns the raw data

        size_t dataSize() const { return Data.size(); }
        uint32_t mapEntryCount() const { return static_cast<uint32_t>(MapEntries.size()); }

        const void* getData() const { return Data.data(); }
        const VanKSpecializationMapEntries* getEntries() const { return MapEntries.data(); }
    };

    struct VanKPipelineShaderStageCreateInfo
    {
        Shader* VanKShader;
        std::optional<VanKSpecializationInfo> specializationInfo;
    };

    struct VanKPipelineVertexInputStateCreateInfo
    {
        BufferLayout VanKBufferLayout;
    };

    enum VankPrimitiveToplogy
    {
        VanK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        VanK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        VanK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
        VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
        VanK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
        VanK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
        VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
        VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
        VanK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
    };

    struct VanKPipelineInputAssemblyStateCreateInfo
    {
        VankPrimitiveToplogy VanKPrimitive;
    };

    enum VanKPolygonMode
    {
        VanK_POLYGON_MODE_FILL,
        VanK_POLYGON_MODE_LINE,
        VanK_POLYGON_MODE_POINT,
        VanK_POLYGON_MODE_FILL_RECTANGLE_NV,
    };

    enum VanKCullModeFlags
    {
        VanK_CULL_MODE_NONE,
        VanK_CULL_MODE_FRONT_BIT,
        VanK_CULL_MODE_BACK_BIT,
        VanK_CULL_MODE_FRONT_AND_BACK,
    };

    enum VanKFrontFace
    {
        VanK_FRONT_FACE_COUNTER_CLOCKWISE,
        VanK_FRONT_FACE_CLOCKWISE,
    };
    
    struct VanKPipelineRasterizationStateCreateInfo
    {
        VanKPolygonMode VanKPolygon;
        VanKCullModeFlags VanKCullMode;
        VanKFrontFace VanKFrontFace;
    };

    enum VanKLogicOp {
        VanK_LOGIC_OP_CLEAR = 0,
        VanK_LOGIC_OP_AND = 1,
        VanK_LOGIC_OP_AND_REVERSE = 2,
        VanK_LOGIC_OP_COPY = 3,
        VanK_LOGIC_OP_AND_INVERTED = 4,
        VanK_LOGIC_OP_NO_OP = 5,
        VanK_LOGIC_OP_XOR = 6,
        VanK_LOGIC_OP_OR = 7,
        VanK_LOGIC_OP_NOR = 8,
        VanK_LOGIC_OP_EQUIVALENT = 9,
        VanK_LOGIC_OP_INVERT = 10,
        VanK_LOGIC_OP_OR_REVERSE = 11,
        VanK_LOGIC_OP_COPY_INVERTED = 12,
        VanK_LOGIC_OP_OR_INVERTED = 13,
        VanK_LOGIC_OP_NAND = 14,
        VanK_LOGIC_OP_SET = 15,
        VanK_LOGIC_OP_MAX_ENUM = 0x7FFFFFFF
    };

    enum VanKBlendFactor
    {
        VanK_BLEND_FACTOR_ZERO,
        VanK_BLEND_FACTOR_ONE,
        VanK_BLEND_FACTOR_SRC_COLOR,
        VanK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VanK_BLEND_FACTOR_DST_COLOR,
        VanK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
        VanK_BLEND_FACTOR_SRC_ALPHA,
        VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VanK_BLEND_FACTOR_DST_ALPHA,
        VanK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
        VanK_BLEND_FACTOR_CONSTANT_COLOR,
        VanK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
        VanK_BLEND_FACTOR_CONSTANT_ALPHA,
        VanK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
        VanK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
        VanK_BLEND_FACTOR_SRC1_COLOR,
        VanK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
        VanK_BLEND_FACTOR_SRC1_ALPHA,
        VanK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
    };
    
    enum VanKBlendOp
    {
        VanK_BLEND_OP_ADD,
        VanK_BLEND_OP_SUBTRACT,
        VanK_BLEND_OP_REVERSE_SUBTRACT,
        VanK_BLEND_OP_MIN,
        VanK_BLEND_OP_MAX,
        VanK_BLEND_OP_ZERO_EXT,
        VanK_BLEND_OP_SRC_EXT,
        VanK_BLEND_OP_DST_EXT,
        VanK_BLEND_OP_SRC_OVER_EXT,
        VanK_BLEND_OP_DST_OVER_EXT,
        VanK_BLEND_OP_SRC_IN_EXT,
        VanK_BLEND_OP_DST_IN_EXT,
        VanK_BLEND_OP_SRC_OUT_EXT,
        VanK_BLEND_OP_DST_OUT_EXT,
        VanK_BLEND_OP_SRC_ATOP_EXT,
        VanK_BLEND_OP_DST_ATOP_EXT,
        VanK_BLEND_OP_XOR_EXT,
        VanK_BLEND_OP_MULTIPLY_EXT,
        VanK_BLEND_OP_SCREEN_EXT,
        VanK_BLEND_OP_OVERLAY_EXT,
        VanK_BLEND_OP_DARKEN_EXT,
        VanK_BLEND_OP_LIGHTEN_EXT,
        VanK_BLEND_OP_COLORDODGE_EXT,
        VanK_BLEND_OP_COLORBURN_EXT,
        VanK_BLEND_OP_HARDLIGHT_EXT,
        VanK_BLEND_OP_SOFTLIGHT_EXT,
        VanK_BLEND_OP_DIFFERENCE_EXT,
        VanK_BLEND_OP_EXCLUSION_EXT,
        VanK_BLEND_OP_INVERT_EXT,
        VanK_BLEND_OP_INVERT_RGB_EXT,
        VanK_BLEND_OP_LINEARDODGE_EXT,
        VanK_BLEND_OP_LINEARBURN_EXT,
        VanK_BLEND_OP_VIVIDLIGHT_EXT,
        VanK_BLEND_OP_LINEARLIGHT_EXT,
        VanK_BLEND_OP_PINLIGHT_EXT,
        VanK_BLEND_OP_HARDMIX_EXT,
        VanK_BLEND_OP_HSL_HUE_EXT,
        VanK_BLEND_OP_HSL_SATURATION_EXT,
        VanK_BLEND_OP_HSL_COLOR_EXT,
        VanK_BLEND_OP_HSL_LUMINOSITY_EXT,
        VanK_BLEND_OP_PLUS_EXT,
        VanK_BLEND_OP_PLUS_CLAMPED_EXT,
        VanK_BLEND_OP_PLUS_CLAMPED_ALPHA_EXT,
        VanK_BLEND_OP_PLUS_DARKER_EXT,
        VanK_BLEND_OP_MINUS_EXT,
        VanK_BLEND_OP_MINUS_CLAMPED_EXT,
        VanK_BLEND_OP_CONTRAST_EXT,
        VanK_BLEND_OP_INVERT_OVG_EXT,
        VanK_BLEND_OP_RED_EXT,
        VanK_BLEND_OP_GREEN_EXT,
        VanK_BLEND_OP_BLUE_EXT,
    };

    enum VanKColorComponentFlagBits
    {
        VanK_COLOR_COMPONENT_R_BIT = 0x00000001,
        VanK_COLOR_COMPONENT_G_BIT,
        VanK_COLOR_COMPONENT_B_BIT,
        VanK_COLOR_COMPONENT_A_BIT,
    };
    using VanKColorComponentFlags = uint32_t;

    struct VanKPipelineColorBlendAttachmentState
    {
        bool blendEnable;
        VanKBlendFactor srcColorBlendFactor;
        VanKBlendFactor dstColorBlendFactor;
        VanKBlendOp colorBlendOp;
        VanKBlendFactor srcAlphaBlendFactor;
        VanKBlendFactor dstAlphaBlendFactor;
        VanKBlendOp alphaBlendOp;
        VanKColorComponentFlags colorWriteMask;
    };

    struct VanKPipelineColorBlendStateCreateInfo
    {
        bool logicOp;
        VanKLogicOp VanKLogicOp;
        std::vector<VanKPipelineColorBlendAttachmentState> VanKColorBlendAttachmentState;
    };

    enum VanKSampleCountFlagBits
    {
        VanK_SAMPLE_COUNT_1_BIT,
        VanK_SAMPLE_COUNT_2_BIT,
        VanK_SAMPLE_COUNT_4_BIT,
        VanK_SAMPLE_COUNT_8_BIT,
        VanK_SAMPLE_COUNT_16_BIT,
        VanK_SAMPLE_COUNT_32_BIT,
        VanK_SAMPLE_COUNT_64_BIT
    };
    
    struct VanKPipelineMultisampleStateCreateInfo
    {
        VanKSampleCountFlagBits sampleCount;
        bool sampleShadingEnable;
        float minSampleShading;
    };

    enum VanKdepthCompareOp
    {
        VanK_COMPARE_OP_NEVER,
        VanK_COMPARE_OP_LESS,
        VanK_COMPARE_OP_EQUAL,
        VanK_COMPARE_OP_LESS_OR_EQUAL,
        VanK_COMPARE_OP_GREATER,
        VanK_COMPARE_OP_NOT_EQUAL,
        VanK_COMPARE_OP_GREATER_OR_EQUAL,
        VanK_COMPARE_OP_ALWAYS,
    };
    
    struct VanKPipelineDepthStencilStateCreateInfo
    {
        bool depthTestEnable;
        bool depthWriteEnable;
        VanKdepthCompareOp VanKdepthCompareOp;
    };

    enum VanKFormat
    {
        VanK_FORMAT_INVALID = 0,
        VanK_Format_B8G8R8A8Srgb,
        VanK_FORMAT_R32_SINT,
    };
    
    struct VanKPipelineRenderingCreateInfo
    {
        std::vector<VanKFormat> VanKColorAttachmentFormats;
    };

    struct VanKGraphicsPipelineSpecification
    {
        VanKPipelineShaderStageCreateInfo ShaderStageCreateInfo;
        VanKPipelineVertexInputStateCreateInfo VertexInputStateCreateInfo;
        VanKPipelineInputAssemblyStateCreateInfo InputAssemblyStateCreateInfo;
        VanKPipelineRasterizationStateCreateInfo RasterizationStateCreateInfo;
        VanKPipelineColorBlendStateCreateInfo ColorBlendStateCreateInfo;
        VanKPipelineMultisampleStateCreateInfo MultisampleStateCreateInfo;
        VanKPipelineDepthStencilStateCreateInfo DepthStateInfo;
        VanKPipelineRenderingCreateInfo RenderingCreateInfo;
    };

    struct VanKComputePipelineCreateInfo
    {
        Shader* VanKShader;
    };

    struct VanKComputePipelineSpecification
    {
        VanKComputePipelineCreateInfo ComputePipelineCreateInfo;
    };

    struct VanKComputePass
    {
        VanKCommandBuffer VanKCommandBuffer;
        VertexBuffer* VanKVertexBuffer;
    };

    enum class VanKPipelineBindPoint
    {
        Graphics,
        Compute
    };

    enum VanKShaderStageFlags
    {
        VanKGraphics,
        VanKCompute
    };

    struct TextureSamplerBinding
    {
        /*const Texture2D* texture;
        const Sampler* sampler;*/
    };

    enum VanKLoadOp
    {
        VanK_LOADOP_LOAD = 0,
        VanK_LOADOP_CLEAR = 1,
        VanK_LOADOP_DONT_CARE = 2,
    };

    enum  VanKStoreOp
    {
        VanK_STOREOP_STORE,
        VanK_STOREOP_DONT_CARE,
        VanK_STOREOP_RESOLVE,
        VanK_STOREOP_RESOLVE_AND_STORE
    };

    struct VanK_FColor
    {
        union
        {
            float f[4];
            int32_t i[4];
            uint32_t u[4];
        };
    };
    
    struct VanKColorTargetInfo
    {
        VanKFormat format;
        VanKLoadOp loadOp;
        VanKStoreOp storeOp;
        VanK_FColor clearColor;
        /*Ref<Texture2D> colorTexture = nullptr;*/ // 
        int arrayElement = 0;
    };

    struct VanKDepthStencilTargetInfo
    {
        VanKLoadOp loadOp;
        VanKStoreOp storeOp;
        VanK_FColor clearColor;
        /*Ref<Texture2D> depthStencilTexture = nullptr;*/ // shadowmap needed
    };

    enum VanKRenderOption
    {
        VanK_Render_None,
        VanK_Render_Swapchain,
        VanK_Render_ImGui
    };

    enum class VanKIndexElementSize
    {
        Uint16,
        Uint32
    };

    struct VanKViewport
    {
        float x = 0.0f;
        float y = 0.0f;
        uint32_t width = 0.0f;
        uint32_t height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    struct VankRect
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct Extent2D { uint32_t width, height; };
    
    enum class RenderAPIType
    {
        None = 0, Vulkan = 1, Metal = 2
    };
    
    class RendererAPI
    {
    public:
        virtual ~RendererAPI() = default;

        // exposing
        virtual void RebuildSwapchain(bool vSyncVal) = 0; 
        virtual ImTextureID getImTextureID(uint32_t index = 0) const = 0;
        virtual void setViewportSize(Extent2D viewportSize) = 0;
        virtual VanKPipeLine createGraphicsPipeline(VanKGraphicsPipelineSpecification pipelineSpecification) = 0;
        virtual VanKPipeLine createComputeShaderPipeline(VanKComputePipelineSpecification computePipelineSpecification) = 0;
        virtual void DestroyAllPipelines() = 0;
        virtual void DestroyPipeline(VanKPipeLine pipeline) = 0;
        virtual VanKCommandBuffer BeginCommandBuffer() { return nullptr; }
        virtual void EndCommandBuffer(VanKCommandBuffer cmd) = 0;
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void BindPipeline(VanKCommandBuffer cmd, VanKPipelineBindPoint pipelineBindPoint, VanKPipeLine pipeline) = 0;
        virtual void BindUniformBuffer(VanKCommandBuffer cmd, VanKPipelineBindPoint bindPoint, UniformBuffer* buffer, uint32_t set, uint32_t binding, uint32_t arrayElement) = 0;
        virtual void BeginRendering(VanKCommandBuffer cmd, const VanKColorTargetInfo* color_target_info, uint32_t num_color_targets, VanKDepthStencilTargetInfo depth_stencil_target_info, VanKRenderOption render_option) = 0;
        virtual void BindFragmentSamplers(VanKCommandBuffer cmd, uint32_t firstSlot, const TextureSamplerBinding* samplers, uint32_t num_bindings) = 0;
        virtual void SetViewport(VanKCommandBuffer cmd, uint32_t viewportCount, const VanKViewport viewport) = 0;
        virtual void SetScissor(VanKCommandBuffer cmd, uint32_t scissorCount, VankRect scissor) = 0;
        virtual void BindVertexBuffer(VanKCommandBuffer cmd, uint32_t first_slot, const VertexBuffer& vertexBuffer, uint32_t num_bindings) = 0;
        virtual void BindIndexBuffer(VanKCommandBuffer cmd, const IndexBuffer& indexBuffer, VanKIndexElementSize elementSize) = 0;
        virtual void Draw(VanKCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
        virtual void DrawIndexed(VanKCommandBuffer cmd, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
        virtual void DrawIndexedIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride) = 0;
        virtual void EndRendering(VanKCommandBuffer cmd) = 0;
        virtual VanKComputePass* BeginComputePass(VanKCommandBuffer cmd, VertexBuffer* buffer = nullptr) = 0;
        virtual void DispatchCompute(VanKComputePass* computePass, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
        virtual void EndComputePass(VanKComputePass* computePass) = 0;
        virtual void waitForGraphicsQueueIdle() = 0;
        //---------
        
        static RenderAPIType GetAPI() { return s_API; }
        static void SetAPI(RenderAPIType api) { s_API = api; }
        
        // --- New: Configuration ---
        struct Config {
            SDL_Window* window = nullptr;
        };
        
        static std::unique_ptr<RendererAPI> Create(const Config& config);
    private:
        static RenderAPIType s_API;
    };
}
