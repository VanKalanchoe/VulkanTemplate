#pragma once
#include <memory>
#include <string>
#include <vector>

#include "Layer.h"
#include "VanK/Core/Window.h"

namespace VanK
{
    class Application;

    struct ApplicationSpecification
    {
        std::string Name = "Application";
        WindowSpecification WindowSpec;
    };

    struct AppState
    {
        Application* app;
        float lastTime;
    };
    
    class Application
    {
    public:
        Application(const ApplicationSpecification& specification = ApplicationSpecification());
        ~Application();

        void Run(AppState& applicationState);

        template<typename TLayer>
        requires(std::is_base_of_v<Layer, TLayer>)
        void PushLayer()
        {
            m_LayerStack.push_back(std::make_unique<TLayer>());    
        }
        
        static Application& Get();
        static float GetTime();
        static std::string GetExecutableRootPath();
        std::shared_ptr<Window> getWindow() { return m_Window; }
    private:
        ApplicationSpecification m_Specification;
        std::shared_ptr<Window> m_Window;

        std::vector<std::unique_ptr<Layer>> m_LayerStack;
    };
}

extern VanK::Application* CreateApplication();