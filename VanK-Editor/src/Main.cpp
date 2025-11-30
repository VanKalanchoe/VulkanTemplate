#include "VanK/Core/Application.h"

#include "EditorLayer.h"

namespace VanK
{
    Application* CreateApplication(ApplicationCommandLineArgs args)
    {
        ApplicationSpecification EditorLayerSpec;
        EditorLayerSpec.Name = "VanK-Editor";
        EditorLayerSpec.WindowSpec.Width = 1920;
        EditorLayerSpec.WindowSpec.Height = 1080;
        EditorLayerSpec.CommandLineArgs = args;

        auto application = new Application(EditorLayerSpec);
        application->PushLayer<EditorLayer>();

        return application;
    }
}