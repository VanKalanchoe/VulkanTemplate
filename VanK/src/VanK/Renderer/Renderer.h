#pragma once
#include "RenderCommand.h"
#include "VanK/Core/Window.h"

namespace VanK
{
    class Renderer
    {
    public:
        static void Init(Window& window);

    public:
        static ShaderLibrary& GetShaderLibrary() { return m_ShaderLibrary; }
    private:
        inline static ShaderLibrary m_ShaderLibrary;
        inline static VanKPipeLine m_GraphicsDebugPipeline = {};
        inline static VanKGraphicsPipelineSpecification m_GraphicsDebugPipelineSpecification = {};
    };
}