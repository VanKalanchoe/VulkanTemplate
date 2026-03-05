#pragma once
#include <memory>
#include <optional>
#include <vector>
#include <SDL3/SDL_video.h>

#include "Shader.h"
#include "VanK/Renderer/Buffer.h"
#include <imgui.h>

#include "VanK/Core/core.h"

namespace VanK
{
    // Forward declare an opaque struct (incomplete type)
    struct VanKCommandBuffer_T;
    using VanKCommandBuffer = VanKCommandBuffer_T*;

    struct VanKTimestampPass
    {
        uint32_t queryIndex;
        uint64_t begin; // GPU timestamp for start
        uint64_t end; // GPU timestamp for end
    };

    struct VanKPipelineStatistics
    {
        uint64_t clippingInvocations;
    };

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
        std::vector<VanKSpecializationMapEntries> MapEntries; // Owns the entries
        std::vector<uint8_t> Data; // Owns the raw data

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
        VanK_CULL_MODE_BACK_BIT,
        VanK_CULL_MODE_FRONT_BIT,
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
        VanKFrontFace VanKFrontFace;
    };

    enum VanKLogicOp
    {
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
        VanK_FORMAT_DEPTH_STENCIL,
        VanK_FORMAT_SWAPCHAIN
    };

    struct VanKPipelineRenderingCreateInfo
    {
        std::vector<VanKFormat> VanKColorAttachmentFormats;
    };

    enum VanKPipelineType
    {
        VanK_Graphics, // Vertex + Fragment
        VanK_Mesh // Mesh (+ optional Task) + Fragment
    };

    struct PushConstantRange
    {
        uint32_t Offset;
        uint32_t Size;
    };

    struct VanKPipelineLayoutCreateInfo
    {
        std::vector<PushConstantRange> PushConstants;
    };

    struct VanKGraphicsPipelineSpecification
    {
        VanKPipelineType PipelineType;
        VanKPipelineShaderStageCreateInfo ShaderStageCreateInfo;

        VanKPipelineVertexInputStateCreateInfo VertexInputStateCreateInfo; // only vertex not mesh
        VanKPipelineInputAssemblyStateCreateInfo InputAssemblyStateCreateInfo; // only vertex not mesh

        VanKPipelineRasterizationStateCreateInfo RasterizationStateCreateInfo;
        VanKPipelineColorBlendStateCreateInfo ColorBlendStateCreateInfo;
        VanKPipelineMultisampleStateCreateInfo MultisampleStateCreateInfo;
        VanKPipelineDepthStencilStateCreateInfo DepthStateInfo;
        VanKPipelineRenderingCreateInfo RenderingCreateInfo;
        VanKPipelineLayoutCreateInfo PipelineLayoutInfo;
    };

    struct VanKComputePipelineCreateInfo
    {
        Shader* VanKShader;
    };

    struct VanKComputePipelineLayoutCreateInfo
    {
        std::vector<PushConstantRange> PushConstants;
    };

    struct VanKComputePipelineSpecification
    {
        VanKComputePipelineCreateInfo ComputePipelineCreateInfo;
        VanKComputePipelineLayoutCreateInfo ComputePipelineLayoutInfo;
    };

    struct VanKComputePass
    {
        VanKCommandBuffer VanKCommandBuffer;
        VertexBuffer* VanKVertexBuffer;
        std::span<Ref<IndirectBuffer>> VanKIndirectBuffers;
        std::span<Ref<IndirectBuffer>> VanKIndirectCountBuffers;
    };

    // A high-value constant for unused shader indices
    #define VANK_SHADER_UNUSED 0xFFFFFFFF
    
    enum class VanKRayTracingGroupType 
    {
        General,
        TrianglesHitGroup,
        ProceduralHitGroup
    };
    
    struct VanKRayTracingGroup 
    {
        VanKRayTracingGroupType type;
    
        // Indices referring to the index in the 'stages' vector
        uint32_t raygenShader       = VANK_SHADER_UNUSED;
        uint32_t missShader         = VANK_SHADER_UNUSED;
        uint32_t closestHitShader   = VANK_SHADER_UNUSED;
        uint32_t anyHitShader       = VANK_SHADER_UNUSED;
        uint32_t intersectionShader = VANK_SHADER_UNUSED;
    };
    
    struct VanKRaytracingPipelineSpecification
    {
        VanKPipelineShaderStageCreateInfo ShaderStageCreateInfo;
        VanKPipelineLayoutCreateInfo PipelineLayoutInfo;
        std::vector<VanKRayTracingGroup> groups;
    };

    enum class VanKPipelineBindPoint
    {
        Graphics,
        Compute,
        Raytracing
    };

    enum VanKShaderStageFlags
    {
        VanKGraphics,
        VanKCompute,
        VanKMesh,
        VanKRaytracing
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

    enum VanKStoreOp
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
        uint32_t imageIndex; // matches ResourceID.index
        VanKFormat format;
        VanKLoadOp loadOp;
        VanKStoreOp storeOp;
        VanK_FColor clearColor;
        /*Ref<Texture2D> colorTexture = nullptr;*/ // 
        int arrayElement = 0;
    };

    struct VanKDepthStencilTargetInfo
    {
        uint32_t imageIndex; // matches ResourceID.index
        VanKFormat format;
        VanKLoadOp loadOp;
        VanKStoreOp storeOp;
        VanK_FColor clearColor;
        /*Ref<Texture2D> depthStencilTexture = nullptr;*/ // shadowmap needed
    };

    // Render Graph
    enum class ResourceUsage
    {
        ComputeRead,
        ComputeWrite,
        StorageRead,
        StorageWrite,
        ColorAttachment,
        ResolveAttachment,
        DepthAttachment,
        ShaderRead,
        IndirectRead,
        TransferSrc,
        TransferDst,
        PresentSrc
    };

    enum class ResourceType : uint8_t
    {
        Image,
        Buffer,
        Dummy
    };

    struct ResourceID
    {
        ResourceType type;
        union
        {
            uint32_t index;      // for images
            VanKBuffer* buffer;  // for buffers
        };

        ResourceID() : type(ResourceType::Dummy), buffer(0) {}
        
        static ResourceID Image(uint32_t idx)
        {
            ResourceID id;
            id.type = ResourceType::Image;
            id.index = idx;
            return id;
        }

        static ResourceID Buffer(VanKBuffer* buf)
        {
            ResourceID id;
            id.type = ResourceType::Buffer;
            id.buffer = buf;
            return id;
        }
        
        static ResourceID Dummy()
        {
            ResourceID id;
            id.type = ResourceType::Dummy;
            id.index = 0;
            return id;
        }

        bool operator==(const ResourceID& other) const
        {
            if (type != other.type) return false;
            switch (type)
            {
            case ResourceType::Image: return index == other.index;
            case ResourceType::Buffer: return buffer == other.buffer;
            case ResourceType::Dummy: return true; // all Dummy resources considered equal
            }
            return false;
        }
    };

    struct ResourceReadWrite
    {
        std::string name;
        ResourceID id;
        ResourceUsage usage;
        std::optional<ResourceUsage> finalUsage;
        VanKFormat format;
        VanKLoadOp loadOp;
        VanKStoreOp storeOp;
        VanK_FColor clearColor;
    };

    struct ResourceState
    {
        enum class Stage
        {
            None,
            TopOfPipe,
            Compute,
            ColorOutput,
            DepthOutput,
            Fragment,
            Transfer,
            DrawIndirect,
            RayTracing
        } stage = Stage::None;
        
        enum class Layout
        {
            Undefined,
            General,
            ColorAttachment,
            ResolveAttachment,
            DepthAttachment,
            ShaderReadOnly,
            TransferSrc,
            TransferDst,
            PresentSrc
        } layout = Layout::Undefined;

        static ResourceState Undefined()
        {
            return {Stage::None, Layout::Undefined};
        }
    };

    // Render Graph----

    enum VanKRenderOption
    {
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
        float width = 0.0f;
        float height = 0.0f;
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

    struct Extent2D
    {
        uint32_t width, height;
    };

    enum class RenderAPIType
    {
        None = 0, Vulkan = 1, Metal = 2
    };

    namespace shaderio
    {
        using namespace glm;
#include "shaderIO.h"
    }
    
    struct ModelHandle
    {
        uint64_t firstPrimitive;
        uint64_t primitiveCount;
    };

    struct ModelPrimitive
    {
        uint64_t primitiveId;
        uint32_t meshletCount;
    };
    
    struct RuntimeModel
    {
        ModelHandle handle;
        std::vector<ModelPrimitive> primitives;
        uint32_t firstInstanceIndex;
        uint32_t instanceCount;
    };
    
    class RendererAPI
    {
    public:
        virtual ~RendererAPI() = default;

        // exposing
        virtual void Shutdown() = 0; // shutdown renderer
        virtual uint32_t GetCurrentFrameIndex() = 0;
        virtual void RebuildSwapchain(bool vSyncVal) = 0;
        virtual void InitImGui() = 0;
        virtual void SetImGuiInit(bool init) = 0;
        virtual ImTextureID getImTextureID(uint32_t index = 0) const = 0;
        virtual void setViewportSize(Extent2D viewportSize) = 0;
        virtual VanKPipeLine createGraphicsPipeline(VanKGraphicsPipelineSpecification pipelineSpecification) = 0;
        virtual VanKPipeLine createComputeShaderPipeline(VanKComputePipelineSpecification computePipelineSpecification) = 0;
        virtual VanKPipeLine createRayTracingPipeline(VanKRaytracingPipelineSpecification raytracingPipelineSpecification) = 0;
        virtual void DestroyAllPipelines() = 0;
        virtual void DestroyPipeline(VanKPipeLine pipeline) = 0;
        virtual VanKCommandBuffer BeginCommandBuffer() { return nullptr; }
        virtual void EndCommandBuffer(VanKCommandBuffer cmd) = 0;
        virtual void BeginFrame(VanKRenderOption renderOption) = 0;
        virtual void EndFrame() = 0;
        virtual void BindPipeline(VanKCommandBuffer cmd, VanKPipelineBindPoint pipelineBindPoint, VanKPipeLine pipeline) = 0;
        virtual void BindUniformBuffer(VanKCommandBuffer cmd, VanKPipelineBindPoint bindPoint, UniformBuffer* buffer, uint32_t set, uint32_t binding, uint32_t arrayElement) = 0;
        virtual void BeginRendering(VanKCommandBuffer cmd, const VanKColorTargetInfo* color_target_info, uint32_t num_color_targets, VanKDepthStencilTargetInfo depth_stencil_target_info) = 0;
        virtual void BindFragmentSamplers(VanKCommandBuffer cmd, uint32_t firstSlot, const TextureSamplerBinding* samplers, uint32_t num_bindings, bool isRayTracing) = 0;
        virtual void BindRayTracing(VanKCommandBuffer cmd, bool useRayQuery, uint32_t renderTargetImageIndex, bool isfinalRenderPass, uint32_t rasterRenderTargetImageIndex, uint32_t rayTraceRenderTargetImageIndex) = 0;
        virtual void SetViewport(VanKCommandBuffer cmd, uint32_t viewportCount, const VanKViewport viewport) = 0;
        virtual void SetScissor(VanKCommandBuffer cmd, uint32_t scissorCount, VankRect scissor) = 0;
        virtual void SetLineWidth(VanKCommandBuffer cmd, float lineWidth) = 0;
        virtual void SetCullMode(VanKCommandBuffer cmd, VanKCullModeFlags cullMode) = 0;
        virtual void BindVertexBuffer(VanKCommandBuffer cmd, uint32_t first_slot, const VertexBuffer& vertexBuffer, uint32_t num_bindings) = 0;
        virtual void BindIndexBuffer(VanKCommandBuffer cmd, const IndexBuffer& indexBuffer, VanKIndexElementSize elementSize) = 0;
        virtual void PushConstans(VanKCommandBuffer cmd, VanKShaderStageFlags stageFlags, uint32_t slot, const void* data, uint32_t dataSize) = 0;
        virtual void Draw(VanKCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
        virtual void DrawIndexed(VanKCommandBuffer cmd, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
        virtual void DrawIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset,
                                       uint32_t maxDrawCount, uint32_t stride) = 0;
        virtual void DrawIndexedIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset,
                                              uint32_t maxDrawCount, uint32_t stride) = 0;
        virtual void DrawMeshTasks(VanKCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
        virtual void DrawMeshTasksIndirect(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, uint32_t maxDrawCount, uint32_t stride) = 0;
        virtual void DrawMeshTasksIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset,
                                                uint32_t maxDrawCount, uint32_t stride) = 0;
        virtual void TraceRays(VanKCommandBuffer cmd, VanKPipeLine rtPipeline, uint32_t width, uint32_t height) = 0;
        virtual void EndRendering(VanKCommandBuffer cmd) = 0;
        virtual void RenderImGui(VanKCommandBuffer cmd) = 0;
        virtual VanKComputePass* BeginComputePass(VanKCommandBuffer cmd, VertexBuffer* vertexBuffer = nullptr, std::span<Ref<IndirectBuffer>> indirectBuffers = {},
                                                  std::span<Ref<IndirectBuffer>> countBuffers = {}) = 0;
        virtual void DispatchCompute(VanKCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
        virtual void EndComputePass(VanKComputePass* computePass) = 0;
        // RenderGraph
        virtual void InsertBarrier(VanKCommandBuffer cmd, ResourceID& id, ResourceState& last, ResourceState& desired) = 0;
        // RenderGraph
        virtual void createBottomLevelASModel(RuntimeModel& model, const StorageBuffer& vertexBuffer, const StorageBuffer& indexBuffer, std::vector<shaderio::MeshletPrimitive>& primitives, std::vector<shaderio::Material>& materials) = 0;
        virtual void createInstanceASModel(RuntimeModel& model, const glm::mat4& modelTransform, const std::vector<shaderio::MeshletPrimitive>& globalPrimitives, std::vector<shaderio::InstanceLUT>& instanceLUTs) = 0;
        virtual void clearAllTopLevelASInstances(std::vector<shaderio::InstanceLUT>& instanceLUTs) = 0;
        virtual void removeInstanceASModel(RuntimeModel& model, const uint64_t& primitiveId, std::vector<shaderio::InstanceLUT>& instanceLUTs) = 0;
        virtual void updateTopLevelASModel(const RuntimeModel& model, const glm::mat4& transform, const uint64_t& primitiveId) = 0;
        virtual void createBottomLevelAS(const StorageBuffer& vertexBuffer, const StorageBuffer& indexBuffer, std::vector<shaderio::MeshletPrimitive>& primitives, std::vector<shaderio::Material>& materials,
                                 std::vector<shaderio::InstanceLUT>& instanceLUTs) = 0;
        virtual void createTopLevelAS() = 0;
   
        virtual void updateTopLevelAS(const glm::mat4& model) = 0;
        virtual void waitForGraphicsQueueIdle() = 0;
        virtual void setEnableTimeStamp(bool temp) = 0;
        virtual bool getEnableTimeStamp() = 0;
        virtual VanKTimestampPass getGPURenderTime() = 0;
        virtual VanKPipelineStatistics getPipelineStatistics() = 0;
        virtual float getTimeStampPeriod() const = 0;
        virtual void StartTimeStamp(VanKCommandBuffer cmd, VanKTimestampPass& pass) = 0;
        virtual void StopTimeStamp(VanKCommandBuffer cmd, VanKTimestampPass& pass) = 0;
        //---------

        static RenderAPIType GetAPI() { return s_API; }
        static void SetAPI(RenderAPIType api) { s_API = api; }

        // --- New: Configuration ---
        struct Config
        {
            SDL_Window* window = nullptr;
        };

        static std::unique_ptr<RendererAPI> Create(const Config& config);

    private:
        static RenderAPIType s_API;
    };
}
