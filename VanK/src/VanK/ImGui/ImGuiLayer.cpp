#include "ImGuiLayer.h"

// Some graphic user interface (GUI) using Dear ImGui
#include "backends/imgui_impl_SDL3.h"
#include "imgui.h"
#include "imgui_internal.h"  // For Docking

namespace VanK
{
    ImGuiLayer::ImGuiLayer()
    {
    }

    ImGuiLayer::~ImGuiLayer()
    {
    }

    void ImGuiLayer::OnUpdate(float ts)
    {
        
    }

    void ImGuiLayer::OnRender()
    {
        
    }

    void ImGuiLayer::OnEvent(Event& event)
    {
        /*ImGui_ImplSDL3_ProcessEvent(event);*/
    }
}
