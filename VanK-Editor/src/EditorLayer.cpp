#include "EditorLayer.h"

#include <print>

#include "VanK/Renderer/Renderer.h"

namespace VanK
{
    EditorLayer::EditorLayer() : m_CameraController(1280.0f / 720.0f)
    {
    
    }

    EditorLayer::~EditorLayer()
    {
    
    }

    void EditorLayer::OnEvent(VanK::Event& event)
    {
        std::println("{}", event.ToString());
        
        VanK::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<VanK::MouseButtonPressedEvent>([this](VanK::MouseButtonPressedEvent& e) { return OnMouseButtonPressed(e); });
    }

    void EditorLayer::OnUpdate(float ts)
    {
    
    }

    void EditorLayer::OnRender()
    {
        Renderer::Flush();
    }

    bool EditorLayer::IsButtonHovered() const
    {
        return true; //maybe useful for imgui ImGui::IsItemHovered()
    }

    bool EditorLayer::OnMouseButtonPressed(VanK::MouseButtonPressedEvent& event)
    {
        if (!IsButtonHovered())
            return false;
        
        /*std::cout << "huren" << std::endl;*/
        
        return true;
    }
}
