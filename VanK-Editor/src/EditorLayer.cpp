#include "EditorLayer.h"

#include "VanK/Renderer/Renderer.h"

namespace VanK
{
    EditorLayer::EditorLayer()
    {
    
    }

    EditorLayer::~EditorLayer()
    {
    
    }

    void EditorLayer::OnUpdate(float ts)
    {
    
    }

    void EditorLayer::OnRender()
    {
        Renderer::Flush();
    }
}
