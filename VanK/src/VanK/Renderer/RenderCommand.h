#pragma once

#include "VanK/Renderer/RendererAPI.h"

namespace VanK
{
    class RenderCommand
    {
    public:
        static void Init()
        {
            s_RendererAPI = RendererAPI::Create(s_Config);
        }

        // function overloads ??? is tht what teh call it

        static void RebuildSwapchain(bool vSyncVal)
        {
            if (s_RendererAPI) s_RendererAPI->RebuildSwapchain(vSyncVal);
        }

        static ImTextureID getImTextureID(uint32_t index = 0)
        {
            return s_RendererAPI ? s_RendererAPI->getImTextureID(index) : -1;
        }

        static void setViewportSize(Extent2D viewportSize)
        {
            if (s_RendererAPI) s_RendererAPI->setViewportSize(viewportSize);
        }

        static VanKPipeLine createGraphicsPipeline(VanKGraphicsPipelineSpecification pipelineSpecification)
        {
            return s_RendererAPI ? s_RendererAPI->createGraphicsPipeline(pipelineSpecification) : nullptr;
        }

        static VanKPipeLine createComputeShaderPipeline(VanKComputePipelineSpecification computePipelineSpecification)
        {
            return s_RendererAPI ? s_RendererAPI->createComputeShaderPipeline(computePipelineSpecification) : nullptr;
        }

        static void DestroyAllPipelines()
        {
            if (s_RendererAPI) s_RendererAPI->DestroyAllPipelines();
        }

        static void DestroyPipeline(VanKPipeLine pipeline)
        {
            if (s_RendererAPI) s_RendererAPI->DestroyPipeline(pipeline);
        }
        
        static VanKCommandBuffer BeginCommandBuffer()
        {
            return s_RendererAPI ? s_RendererAPI->BeginCommandBuffer() : nullptr;
        }

        static void EndCommandBuffer(VanKCommandBuffer cmd)
        {
            if (s_RendererAPI) s_RendererAPI->EndCommandBuffer(cmd);
        }

        static void BeginFrame()
        {
            if (s_RendererAPI) s_RendererAPI->BeginFrame();
        }

        static void EndFrame()
        {
            if (s_RendererAPI) s_RendererAPI->EndFrame();
        }
        
        static void BindPipeline(VanKCommandBuffer cmd, VanKPipelineBindPoint pipelineBindPoint, VanKPipeLine pipeline)
        {
            if (s_RendererAPI) s_RendererAPI->BindPipeline(cmd, pipelineBindPoint, pipeline);
        }

        static void BindUniformBuffer(VanKCommandBuffer cmd, VanKPipelineBindPoint bindPoint, UniformBuffer* buffer, uint32_t set, uint32_t binding, uint32_t arrayElement)
        {
            if (s_RendererAPI) s_RendererAPI->BindUniformBuffer(cmd, bindPoint, buffer, set, binding, arrayElement);
        }

        static void BeginRendering(VanKCommandBuffer cmd, const VanKColorTargetInfo* color_target_info, uint32_t num_color_targets, VanKDepthStencilTargetInfo depth_stencil_target_info, VanKRenderOption render_option)
        {
            if (s_RendererAPI) s_RendererAPI->BeginRendering(cmd, color_target_info, num_color_targets, depth_stencil_target_info, render_option);
        }

        static void BindFragmentSamplers(VanKCommandBuffer cmd, uint32_t firstSlot, const TextureSamplerBinding* samplers, uint32_t num_bindings)
        {
            if (s_RendererAPI) s_RendererAPI->BindFragmentSamplers(cmd, firstSlot, samplers, num_bindings);
        }
        
        static void SetViewport(VanKCommandBuffer cmd, uint32_t viewportCount, const VanKViewport viewport)
        {
            if (s_RendererAPI) s_RendererAPI->SetViewport(cmd, viewportCount, viewport);
        }

        static void SetScissor(VanKCommandBuffer cmd, uint32_t scissorCount, const VankRect scissor)
        {
            if (s_RendererAPI) s_RendererAPI->SetScissor(cmd, scissorCount, scissor);
        }

        static void BindVertexBuffer(VanKCommandBuffer cmd, uint32_t first_slot, const VertexBuffer& vertexBuffer, uint32_t num_bindings)
        {
            if (s_RendererAPI) s_RendererAPI->BindVertexBuffer(cmd, first_slot, vertexBuffer, num_bindings);
        }

        static void BindIndexBuffer(VanKCommandBuffer cmd, const IndexBuffer& indexBuffer, VanKIndexElementSize elementSize)
        {
            if (s_RendererAPI) s_RendererAPI->BindIndexBuffer(cmd, indexBuffer, elementSize);
        }

        static void DrawIndexed(VanKCommandBuffer cmd, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
        {
            if (s_RendererAPI) s_RendererAPI->DrawIndexed(cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }
        
        static void DrawIndexedIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
        {
            if (s_RendererAPI) s_RendererAPI->DrawIndexedIndirectCount(cmd, indirectBuffer, indirectBufferOffset, countBuffer, countBufferOffset, maxDrawCount, stride);       
        }

        static void EndRendering(VanKCommandBuffer cmd)
        {
            if (s_RendererAPI) s_RendererAPI->EndRendering(cmd);
        }

        /**
        * @brief Begins a compute pass with an optional vertex buffer
        *
        * To prevent vertex buffer usage while Computing provide it here or no guarantee to sync.
        * 
        * Inserts memory barriers to ensure safe access before compute operations.
        * 
        * @param cmd The Vulkan command buffer to record into.
        * @param buffer Optional vertex buffer to write to during the compute pass.
        *               If null, no write barrier is added.
        * 
        * @return A pointer to the created compute pass handle.
        */
        static VanKComputePass* BeginComputePass(VanKCommandBuffer cmd, VertexBuffer* buffer = nullptr)
        {
            return s_RendererAPI ? s_RendererAPI->BeginComputePass(cmd, buffer) : nullptr;
        }

        static void DispatchCompute(VanKComputePass* computePass, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
        {
            if (s_RendererAPI) s_RendererAPI->DispatchCompute(computePass, groupCountX, groupCountY, groupCountZ);
        }

        static void EndComputePass(VanKComputePass* computePass)
        {
            if (s_RendererAPI) s_RendererAPI->EndComputePass(computePass);
        }

        static void waitForGraphicsQueueIdle()
        {
            if (s_RendererAPI) s_RendererAPI->waitForGraphicsQueueIdle();
        }
        
        static void SetConfig(const RendererAPI::Config& cfg)
        {
            s_Config = cfg;
        }
        
        static RendererAPI* GetRendererAPI()
        {
            return s_RendererAPI.get();
        }

    private:
        static RendererAPI::Config s_Config;
        static std::unique_ptr<RendererAPI> s_RendererAPI;
    };
}
