#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Log.h"
#include "ProfilerAPI.h"
#include "VanK/Core/core.h"
#include "VanK/Core/Layer.h"
#include "VanK/Core/Window.h"
#include "VanK/Events/Event.h"

namespace VanK
{
    class Application;
    
    struct ApplicationCommandLineArgs
    {
        int Count = 0;
        char** Args = nullptr;

        const char* operator[](int index) const
        {
            VK_CORE_ASSERT(index < Count, "index out of range");
            return Args[index];
        }
    };

    struct ApplicationSpecification
    {
        std::string Name = "Application";
        std::string WorkingDirectory;
        //app args ApplicationCommandLineArgs CommandLineArgs;
        ApplicationCommandLineArgs CommandLineArgs;
        WindowSpecification WindowSpec;
        
        using EventCallbackFn = std::function<void(Event&)>;
        EventCallbackFn EventCallback;
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
        
        void RaiseEvent(Event& event);

        template<typename TLayer>
        requires(std::is_base_of_v<Layer, TLayer>)
        void PushLayer()
        {
            m_LayerStack.push_back(std::make_unique<TLayer>());    
        }
        
        template<typename TLayer>
        requires(std::is_base_of_v<Layer, TLayer>)
        void PopLayer()
        {
            auto it = std::find_if(m_LayerStack.begin(), m_LayerStack.end(), [](const std::unique_ptr<Layer>& layer) {
                return dynamic_cast<TLayer*>(layer.get()) != nullptr;
            });

            if (it != m_LayerStack.end())
                m_LayerStack.erase(it); // layer is destroyed here
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
        const ApplicationSpecification& GetSpecification() const { return m_Specification; }
        static void Shutdown();
        void BlockEvents(bool block) { m_BlockEvents = block; }
        bool m_BlockEvents = false;
    private:
        std::vector<std::function<void()>> m_MainThreadQueue;
        std::mutex m_MainThreadQueueMutex;
        
        ApplicationSpecification m_Specification;
        std::shared_ptr<Window> m_Window;

        std::vector<std::unique_ptr<Layer>> m_LayerStack;
    };
    
    extern Application* CreateApplication(ApplicationCommandLineArgs args);
}