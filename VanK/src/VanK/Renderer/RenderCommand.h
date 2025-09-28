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

        static void Render()
        {
            if (s_RendererAPI) s_RendererAPI->Render();
        }

        static VanKPipeLine createGraphicsPipeline(VanKGraphicsPipelineSpecification pipelineSpecification)
        {
            return s_RendererAPI ? s_RendererAPI->createGraphicsPipeline(pipelineSpecification) : nullptr;
        }

        static void BindPipeline(VanKCommandBuffer cmd, VanKPipelineBindPoint pipelineBindPoint, VanKPipeLine pipeline)
        {
            if (s_RendererAPI) s_RendererAPI->BindPipeline(cmd, pipelineBindPoint, pipeline);
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
