#include "Application.h"

#include <cassert>
#include <iostream>
#include <glm/common.hpp>

#define SDL_MAIN_USE_CALLBACKS
#include <backends/imgui_impl_SDL3.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_timer.h>

#include "Log.h"
#include "VanK/ImGui/ImGuiLayer.h"
#include "VanK/Renderer/Renderer.h"

namespace VanK
{
    static Application* s_Application = nullptr;

    Application::Application(const ApplicationSpecification& specification) : m_Specification(specification)
    {
        s_Application = this;

        if (m_Specification.WindowSpec.Title.empty())
            m_Specification.WindowSpec.Title = m_Specification.Name;

        m_Window = std::make_shared<Window>(m_Specification.WindowSpec);
        
        m_Window->initWindow();

        Renderer::Init(*m_Window);

        /*// ImGui
        PushLayer<ImGuiLayer>();*/
    }

    Application::~Application()
    {
        Renderer::Shutdown();
        
        m_Window->Destroy();

        s_Application = nullptr;
    }

    void Application::Run(AppState& applicationState)
    {
        float currentTime = applicationState.app->GetTime();
        float timestep = glm::clamp(currentTime - applicationState.lastTime, 0.001f, 0.1f);
        applicationState.lastTime = currentTime;

        // Main layer update here
        for (const std::unique_ptr<Layer>& layer : m_LayerStack)
            layer->OnUpdate(timestep);

        // NOTE: rendering can be done elsewhere (eg. render thread)
        for (const std::unique_ptr<Layer>& layer : m_LayerStack)
            layer->OnRender();
    }

    Application& Application::Get()
    {
        assert(s_Application);
        return *s_Application;
    }

    void Application::SubmitToMainThread(const std::function<void()>& function)
    {
        std::scoped_lock<std::mutex> lock(m_MainThreadQueueMutex);
        
        m_MainThreadQueue.emplace_back(function);
    }

    void Application::ExecuteMainThreadQueue()
    {
        std::vector<std::function<void()>> copy;
        {
            std::scoped_lock<std::mutex> lock(m_MainThreadQueueMutex);
            copy = m_MainThreadQueue;
            m_MainThreadQueue.clear();
        }
        
        for (auto& func : copy)
            func();
    }
    
    extern "C" {
        SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
        {
            SDL_SetAppMetadata("Vulkan Engine", "1.0", "com.example.engine");
            
            try
            {
                Log::Init();
                
                auto applicationState = new AppState();
                applicationState->app = CreateApplication();
                applicationState->lastTime = Application::GetTime();
                *appstate = applicationState;
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << '\n';
                return SDL_APP_FAILURE;
            }

            return SDL_APP_CONTINUE;
        }

        SDL_AppResult SDL_AppIterate(void* appstate)
        {
            auto applicationState = static_cast<AppState*>(appstate);

            applicationState->app->ExecuteMainThreadQueue();
            applicationState->app->Run(*applicationState);
            
            return SDL_APP_CONTINUE;
        }

        SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
        {
            auto applicationState = static_cast<AppState*>(appstate);

            ImGui_ImplSDL3_ProcessEvent(event);

            switch (event->type)
            {
            case SDL_EVENT_QUIT:
                {
                    return SDL_APP_SUCCESS;
                }
            case SDL_EVENT_WINDOW_RESIZED:
                {
                    /*app->setFramebufferResized(true);*/
                    return SDL_APP_CONTINUE;
                }
            case SDL_EVENT_WINDOW_MINIMIZED:
                {
                    /*app->setWindowMinimized(true);*/
                    return SDL_APP_CONTINUE;
                }
            case SDL_EVENT_WINDOW_RESTORED:
                {
                    /*app->setWindowMinimized(false);
                    app->setFramebufferResized(true); // force swapchain recreation*/
                    return SDL_APP_CONTINUE;
                }
            case SDL_EVENT_KEY_DOWN:
                {
                    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
                    {
                        return SDL_APP_SUCCESS;
                    }
                }
            default:
                return SDL_APP_CONTINUE;
            }
        }

        void SDL_AppQuit(void* appstate, SDL_AppResult result)
        {
            auto applicationState = static_cast<AppState*>(appstate);
            delete applicationState->app;
            delete applicationState;
        }
    }
    
    float Application::GetTime()
    {
        return static_cast<float>(SDL_GetTicks()) / 1000.0f;
    }

    std::string Application::GetExecutableRootPath()
    {
        return SDL_GetBasePath();
    }
}
