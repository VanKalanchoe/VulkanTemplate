#include <memory>
#ifdef __INTELLISENSE__
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <vulkan/vk_platform.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_log.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

class HelloTriangleApplication
{
public:
    void init()
    {
        initWindow();
        initVulkan();
    }

    void render()
    {
        mainLoop();
    }

    void shutdown()
    {
        cleanup();
    }

private:
    SDL_Window* window = nullptr;
    
    SDL_AppResult initWindow()
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        
        window = SDL_CreateWindow("Vulkan", WIDTH, HEIGHT, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

        if (!window)
        {
            SDL_Log("Couldn't create window: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        //SDL_SetWindowMinimumSize(window,640,480);
        
        return SDL_APP_CONTINUE;
    }

    SDL_AppResult initVulkan()
    {
        std::cout << "Hello, Vulkan!" << std::endl;
        return SDL_APP_CONTINUE;
    }

    void mainLoop()
    {
    }

    void cleanup()
    {
        SDL_DestroyWindow(window);
    }
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    SDL_SetAppMetadata("Example Renderer Clear", "1.0", "com.example.renderer-clear");
    
    try
    {
        auto app = std::make_unique<HelloTriangleApplication>();
        app->init();
        
        *appstate = app.release();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto app = static_cast<HelloTriangleApplication*>(appstate);
    app->render();
    
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    switch (event->type)
    {
    case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
    default: return SDL_APP_CONTINUE;
    }
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    auto app = static_cast<HelloTriangleApplication*>(appstate);
    app->shutdown();
    delete app;
}