#include "VanK/Core/Application.h"

#include "EditorLayer.h"

VanK::Application* CreateApplication()
{
    VanK::ApplicationSpecification EditorLayerSpec;
    EditorLayerSpec.Name = "VanK-Editor";
    EditorLayerSpec.WindowSpec.Width = 1920;
    EditorLayerSpec.WindowSpec.Height = 1080;

    auto application = new VanK::Application(EditorLayerSpec);
    application->PushLayer<EditorLayer>();

    return application;
}