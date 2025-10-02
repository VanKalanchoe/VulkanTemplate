#include "VanK/Core/Application.h"

#include "EditorLayer.h"

namespace VanK
{
    Application* CreateApplication()
    {
        ApplicationSpecification EditorLayerSpec;
        EditorLayerSpec.Name = "VanK-Editor";
        EditorLayerSpec.WindowSpec.Width = 1920;
        EditorLayerSpec.WindowSpec.Height = 1080;

        auto application = new Application(EditorLayerSpec);
        application->PushLayer<EditorLayer>();

        return application;
    }
}