#pragma once
#include <functional>
#include <memory>
#include <mutex>
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

        template<typename TLayer>
        requires(std::is_base_of_v<Layer, TLayer>)
        TLayer* GetLayer()
        {
            for (const auto& layer : m_LayerStack)
            {
                if (auto casted = dynamic_cast<TLayer*>(layer.get()))
                    return casted;
            }
            return nullptr;
        }
        
        static Application& Get();
        void SubmitToMainThread(const std::function<void()>& function);
        void ExecuteMainThreadQueue();
        static float GetTime();
        static std::string GetExecutableRootPath();
        std::shared_ptr<Window> getWindow() { return m_Window; }
    private:
        std::vector<std::function<void()>> m_MainThreadQueue;
        std::mutex m_MainThreadQueueMutex;
        
        ApplicationSpecification m_Specification;
        std::shared_ptr<Window> m_Window;

        std::vector<std::unique_ptr<Layer>> m_LayerStack;
    };
    
    extern Application* CreateApplication();
}